#!/usr/bin/env python3
"""
mkfat12.py — z_tools/edimg.exe 의 (harib27f 용) 순수 Python 대체.

원본 edimg는 OSASK 압축(.tek) 템플릿에서 시작했지만, 여기서는 동일한 1.44MB
FAT12 플로피 이미지를 빈 상태에서 생성한 뒤 부트섹터 오버라이드 + 파일 복사를
수행합니다. 결과 이미지는 QEMU/Bochs/실기에서 그대로 부팅됩니다.

사용법:
    mkfat12.py --boot ipl09.bin --out haribote.img --label HARIBOTEOS \\
               file1 file2 dir/file3 ...

옵션:
    --boot      앞 512바이트로 들어갈 부트섹터(IPL) 파일.
    --out       출력 이미지 경로.
    --label     볼륨 라벨 (최대 11자, 기본 'HARIBOTEOS').
    --oem       OEM ID (8자, 기본 'HARIBOTE').
    위치 인자   루트에 복사할 파일들. 8.3 변환은 단순 절단(원본 edimg와 동일).
"""

from __future__ import annotations
import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
TOTAL_SECTORS = 2880      # 1.44 MB
RESERVED = 1              # boot sector
FAT_COUNT = 2
SECTORS_PER_FAT = 9
ROOT_ENTRIES = 224
ROOT_SECTORS = (ROOT_ENTRIES * 32 + SECTOR - 1) // SECTOR  # 14
SECTORS_PER_CLUSTER = 1
DATA_START_SECTOR = RESERVED + FAT_COUNT * SECTORS_PER_FAT + ROOT_SECTORS  # 33
DATA_SECTORS = TOTAL_SECTORS - DATA_START_SECTOR
CLUSTER_COUNT = DATA_SECTORS // SECTORS_PER_CLUSTER  # 2847

# FAT12 cluster numbering: cluster 2 = first data cluster (LBA = DATA_START_SECTOR)


def make_boot_sector(label: str, oem: str) -> bytes:
    """기본 부트섹터(BPB만 채우고 IPL은 NOP). --boot 로 이후에 오버라이드 가능."""
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"  # JMP +0x3C ; NOP
    bs[3:11] = oem.encode("ascii").ljust(8, b" ")[:8]
    struct.pack_into("<H", bs, 11, SECTOR)                # bytes/sector
    bs[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", bs, 14, RESERVED)              # reserved
    bs[16] = FAT_COUNT
    struct.pack_into("<H", bs, 17, ROOT_ENTRIES)
    struct.pack_into("<H", bs, 19, TOTAL_SECTORS)
    bs[21] = 0xF0                                         # media: 1.44MB
    struct.pack_into("<H", bs, 22, SECTORS_PER_FAT)
    struct.pack_into("<H", bs, 24, 18)                    # sectors/track
    struct.pack_into("<H", bs, 26, 2)                     # heads
    struct.pack_into("<I", bs, 28, 0)                     # hidden
    struct.pack_into("<I", bs, 32, 0)                     # large total
    bs[36] = 0x00                                         # drive
    bs[37] = 0x00
    bs[38] = 0x29                                         # ext boot sig
    struct.pack_into("<I", bs, 39, 0xFFFFFFFF)            # serial
    bs[43:54] = label.encode("ascii").ljust(11, b" ")[:11]
    bs[54:62] = b"FAT12   "
    bs[510:512] = b"\x55\xAA"
    return bytes(bs)


def split_8_3(name: str) -> tuple[bytes, bytes]:
    """파일명을 FAT12 8.3 형식으로(공백패딩, 대문자). 단순 절단(원본 호환)."""
    base, _, ext = name.rpartition(".")
    if not base:
        base, ext = ext, ""
    base = base.upper()[:8]
    ext = ext.upper()[:3]
    return (
        base.encode("ascii", errors="replace").ljust(8, b" "),
        ext.encode("ascii", errors="replace").ljust(3, b" "),
    )


def build_fat12(used_chains: list[list[int]]) -> bytes:
    """클러스터 체인 목록 → 9섹터(=4608B) FAT12 이미지."""
    # FAT 엔트리 배열. cluster 0 = media descriptor, cluster 1 = EOC
    entries = [0] * (CLUSTER_COUNT + 2)
    entries[0] = 0xFF0
    entries[1] = 0xFFF
    for chain in used_chains:
        for k, c in enumerate(chain):
            entries[c] = chain[k + 1] if k + 1 < len(chain) else 0xFFF
    # pack 12-bit LE 쌍을 3바이트로
    fat = bytearray(SECTORS_PER_FAT * SECTOR)
    for i in range(0, len(entries), 2):
        a = entries[i] & 0xFFF
        b = entries[i + 1] & 0xFFF if i + 1 < len(entries) else 0
        off = (i // 2) * 3
        if off + 2 >= len(fat):
            break
        fat[off]     = a & 0xFF
        fat[off + 1] = ((a >> 8) & 0x0F) | ((b & 0x0F) << 4)
        fat[off + 2] = (b >> 4) & 0xFF
    return bytes(fat)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--boot")
    p.add_argument("--out", required=True)
    p.add_argument("--label", default="HARIBOTEOS")
    p.add_argument("--oem", default="HARIBOTE")
    p.add_argument("files", nargs="*")
    args = p.parse_args(argv[1:])

    image = bytearray(TOTAL_SECTORS * SECTOR)

    # 1. 부트섹터(기본 BPB)
    image[:SECTOR] = make_boot_sector(args.label, args.oem)

    # 2. 파일 배치
    next_cluster = 2
    chains: list[list[int]] = []
    dir_entries: list[bytes] = []

    for f in args.files:
        path = Path(f)
        data = path.read_bytes()
        cluster_count = max(1, (len(data) + SECTOR - 1) // SECTOR)
        chain = list(range(next_cluster, next_cluster + cluster_count))
        if chain[-1] >= CLUSTER_COUNT + 2:
            print(f"[error] 디스크 가득 참: {f}", file=sys.stderr)
            return 1
        next_cluster += cluster_count
        chains.append(chain)

        # 데이터 영역에 쓰기
        for k, c in enumerate(chain):
            off = (DATA_START_SECTOR + (c - 2)) * SECTOR
            chunk = data[k * SECTOR : (k + 1) * SECTOR]
            image[off : off + len(chunk)] = chunk

        # 디렉터리 엔트리(32B)
        base, ext = split_8_3(path.name)
        entry = bytearray(32)
        entry[0:8] = base
        entry[8:11] = ext
        entry[11] = 0x20                     # archive
        struct.pack_into("<H", entry, 26, chain[0])
        struct.pack_into("<I", entry, 28, len(data))
        dir_entries.append(bytes(entry))

    if len(dir_entries) > ROOT_ENTRIES:
        print(f"[error] 루트 엔트리 한계 초과 ({len(dir_entries)} > {ROOT_ENTRIES})",
              file=sys.stderr)
        return 1

    # 3. FAT
    fat = build_fat12(chains)
    fat_off = RESERVED * SECTOR
    image[fat_off : fat_off + len(fat)] = fat
    fat_off2 = (RESERVED + SECTORS_PER_FAT) * SECTOR
    image[fat_off2 : fat_off2 + len(fat)] = fat

    # 4. 루트 디렉터리
    root_off = (RESERVED + FAT_COUNT * SECTORS_PER_FAT) * SECTOR
    for i, e in enumerate(dir_entries):
        image[root_off + i * 32 : root_off + (i + 1) * 32] = e

    # 5. 부트섹터 오버라이드 (BPB는 살리고 코드 영역만 교체하는 것이 정석이지만,
    #    원본 edimg/wbinimg 도 그대로 덮어쓰므로 동일하게 진행. ipl09.nas 가
    #    BPB 위치를 알고 자기 데이터를 적절히 배치하므로 실 사용에 문제 없음.)
    if args.boot:
        boot = Path(args.boot).read_bytes()
        if len(boot) > SECTOR:
            print(f"[warn] 부트섹터가 {len(boot)}바이트 — 처음 {SECTOR}바이트만 씀",
                  file=sys.stderr)
        image[: min(SECTOR, len(boot))] = boot[:SECTOR]

    Path(args.out).write_bytes(bytes(image))
    print(f"[mkfat12.py] wrote {args.out} "
          f"({TOTAL_SECTORS * SECTOR // 1024} KB, {len(args.files)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
