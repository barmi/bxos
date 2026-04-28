#!/usr/bin/env python3
"""
mkfat12.py — FAT12 / FAT16 디스크 이미지 빌더 (z_tools/edimg.exe 의 순수 Python 대체).

원본 이름은 mkfat12 지만 work1 (FS 확장) 작업으로 FAT16 모드가 추가되었다.
이름은 호환성을 위해 그대로 유지한다.

사용법:
    # 기존 1.44MB FAT12 플로피 (기본값, ipl09.nas 부트섹터 필수)
    mkfat12.py --boot ipl09.bin --out haribote.img file1 file2 ...

    # 32MB FAT16 데이터 디스크 (부트섹터 불필요)
    mkfat12.py --fs fat16 --size 32M --out data.img file1 file2 ...

옵션:
    --fs        fat12 (기본) | fat16
    --size      이미지 크기. 숫자(byte) 또는 K/M 접미사. fat12 는 항상 1.44MB로 강제.
    --boot      앞 512바이트로 들어갈 부트섹터(IPL) 파일. fat16 데이터 디스크에서는 생략 가능.
    --out       출력 이미지 경로.
    --label     볼륨 라벨 (최대 11자, 기본 'HARIBOTEOS' / 'BXOSDATA').
    --oem       OEM ID (8자, 기본 'HARIBOTE').
    위치 인자   루트에 복사할 파일들. 8.3 변환은 단순 절단(원본 edimg와 동일).
"""

from __future__ import annotations
import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SECTOR = 512


@dataclass
class FsParams:
    fs: str                    # "fat12" or "fat16"
    total_sectors: int
    sectors_per_cluster: int
    reserved: int
    fat_count: int
    sectors_per_fat: int
    root_entries: int
    media: int
    sectors_per_track: int
    heads: int
    drive_num: int
    fs_type_label: bytes       # "FAT12   " or "FAT16   "

    @property
    def root_sectors(self) -> int:
        return (self.root_entries * 32 + SECTOR - 1) // SECTOR

    @property
    def data_start_sector(self) -> int:
        return self.reserved + self.fat_count * self.sectors_per_fat + self.root_sectors

    @property
    def data_sectors(self) -> int:
        return self.total_sectors - self.data_start_sector

    @property
    def cluster_count(self) -> int:
        return self.data_sectors // self.sectors_per_cluster

    @property
    def eoc(self) -> int:
        return 0xFFF if self.fs == "fat12" else 0xFFFF


def _params_fat12() -> FsParams:
    return FsParams(
        fs="fat12",
        total_sectors=2880,        # 1.44 MB
        sectors_per_cluster=1,
        reserved=1,
        fat_count=2,
        sectors_per_fat=9,
        root_entries=224,
        media=0xF0,                # 1.44MB removable
        sectors_per_track=18,
        heads=2,
        drive_num=0x00,
        fs_type_label=b"FAT12   ",
    )


def _params_fat16(total_bytes: int) -> FsParams:
    if total_bytes % SECTOR != 0:
        raise SystemExit(f"[error] FAT16 크기는 섹터(512B) 배수여야 합니다: {total_bytes}")
    total_sectors = total_bytes // SECTOR
    # FAT16 권장: cluster size 2KB (sec/clus = 4), root entries 512.
    # 너무 작거나 크면 FAT16 cluster count 범위(4085~65524)를 벗어날 수 있다.
    sectors_per_cluster = 4
    reserved = 1
    fat_count = 2
    root_entries = 512
    root_sectors = (root_entries * 32 + SECTOR - 1) // SECTOR

    # FATSz 를 반복법으로 계산.
    fat_sz = 1
    for _ in range(64):  # 충분히 수렴
        data_sectors = total_sectors - reserved - fat_count * fat_sz - root_sectors
        clusters = data_sectors // sectors_per_cluster
        needed_bytes = (clusters + 2) * 2
        new_fat_sz = (needed_bytes + SECTOR - 1) // SECTOR
        if new_fat_sz == fat_sz:
            break
        fat_sz = new_fat_sz
    else:
        raise SystemExit("[error] FAT16 FATSz 계산 수렴 실패")

    # FAT16 cluster 개수 검증.
    data_sectors = total_sectors - reserved - fat_count * fat_sz - root_sectors
    clusters = data_sectors // sectors_per_cluster
    if clusters < 4085:
        raise SystemExit(
            f"[error] cluster count {clusters} < 4085 — FAT16 한계 미달. "
            f"--size 를 키우거나 sectors_per_cluster 를 낮추세요.")
    if clusters > 65524:
        raise SystemExit(
            f"[error] cluster count {clusters} > 65524 — FAT16 한계 초과. "
            f"sectors_per_cluster 를 키우세요.")

    return FsParams(
        fs="fat16",
        total_sectors=total_sectors,
        sectors_per_cluster=sectors_per_cluster,
        reserved=reserved,
        fat_count=fat_count,
        sectors_per_fat=fat_sz,
        root_entries=root_entries,
        media=0xF8,                # fixed disk
        sectors_per_track=63,
        heads=16,
        drive_num=0x80,
        fs_type_label=b"FAT16   ",
    )


def _parse_size(s: str) -> int:
    s = s.strip().upper()
    mult = 1
    if s.endswith("K"):
        mult = 1024
        s = s[:-1]
    elif s.endswith("M"):
        mult = 1024 * 1024
        s = s[:-1]
    elif s.endswith("G"):
        mult = 1024 * 1024 * 1024
        s = s[:-1]
    return int(float(s) * mult)


def make_boot_sector(fp: FsParams, label: str, oem: str) -> bytes:
    """기본 부트섹터(BPB만 채우고 IPL은 NOP). --boot 로 이후에 오버라이드 가능."""
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"  # JMP +0x3C ; NOP
    bs[3:11] = oem.encode("ascii").ljust(8, b" ")[:8]
    struct.pack_into("<H", bs, 11, SECTOR)
    bs[13] = fp.sectors_per_cluster
    struct.pack_into("<H", bs, 14, fp.reserved)
    bs[16] = fp.fat_count
    struct.pack_into("<H", bs, 17, fp.root_entries)
    if fp.total_sectors < 0x10000:
        struct.pack_into("<H", bs, 19, fp.total_sectors)   # TotSec16
        struct.pack_into("<I", bs, 32, 0)                  # TotSec32
    else:
        struct.pack_into("<H", bs, 19, 0)
        struct.pack_into("<I", bs, 32, fp.total_sectors)
    bs[21] = fp.media
    struct.pack_into("<H", bs, 22, fp.sectors_per_fat)
    struct.pack_into("<H", bs, 24, fp.sectors_per_track)
    struct.pack_into("<H", bs, 26, fp.heads)
    struct.pack_into("<I", bs, 28, 0)                      # hidden
    bs[36] = fp.drive_num
    bs[37] = 0x00                                          # reserved (NT flags)
    bs[38] = 0x29                                          # ext boot sig
    struct.pack_into("<I", bs, 39, 0xFFFFFFFF)             # serial
    bs[43:54] = label.encode("ascii").ljust(11, b" ")[:11]
    bs[54:62] = fp.fs_type_label
    bs[510:512] = b"\x55\xAA"
    return bytes(bs)


def split_8_3(name: str) -> tuple[bytes, bytes]:
    """파일명을 FAT 8.3 형식으로(공백패딩, 대문자). 단순 절단(원본 호환)."""
    base, _, ext = name.rpartition(".")
    if not base:
        base, ext = ext, ""
    base = base.upper()[:8]
    ext = ext.upper()[:3]
    return (
        base.encode("ascii", errors="replace").ljust(8, b" "),
        ext.encode("ascii", errors="replace").ljust(3, b" "),
    )


def build_fat(fp: FsParams, used_chains: list[list[int]]) -> bytes:
    """클러스터 체인 목록 → FAT 한 카피 raw bytes (FAT12/FAT16 공통 진입점)."""
    entries = [0] * (fp.cluster_count + 2)
    # cluster 0: media descriptor를 low byte 에. cluster 1: EOC.
    entries[0] = (0xFFFFFF00 | fp.media) & 0xFFFFFFFF
    entries[1] = 0xFFFFFFFF
    for chain in used_chains:
        for k, c in enumerate(chain):
            entries[c] = chain[k + 1] if k + 1 < len(chain) else fp.eoc

    fat = bytearray(fp.sectors_per_fat * SECTOR)
    if fp.fs == "fat12":
        # 12-bit packed pairs → 3 bytes
        entries[0] &= 0xFFF
        entries[1] &= 0xFFF
        for i in range(0, len(entries), 2):
            a = entries[i] & 0xFFF
            b = entries[i + 1] & 0xFFF if i + 1 < len(entries) else 0
            off = (i // 2) * 3
            if off + 2 >= len(fat):
                break
            fat[off]     = a & 0xFF
            fat[off + 1] = ((a >> 8) & 0x0F) | ((b & 0x0F) << 4)
            fat[off + 2] = (b >> 4) & 0xFF
    else:  # fat16
        for i, v in enumerate(entries):
            off = i * 2
            if off + 2 > len(fat):
                break
            struct.pack_into("<H", fat, off, v & 0xFFFF)
    return bytes(fat)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--fs", choices=["fat12", "fat16"], default="fat12")
    p.add_argument("--size", help="이미지 크기 (예: 32M). fat16 에서만 의미 있음.")
    p.add_argument("--boot")
    p.add_argument("--out", required=True)
    p.add_argument("--label")
    p.add_argument("--oem", default="HARIBOTE")
    p.add_argument("files", nargs="*")
    args = p.parse_args(argv[1:])

    if args.fs == "fat12":
        fp = _params_fat12()
        default_label = "HARIBOTEOS"
    else:
        if not args.size:
            return _die("--fs fat16 에는 --size 가 필요합니다 (예: --size 32M)")
        fp = _params_fat16(_parse_size(args.size))
        default_label = "BXOSDATA"

    label = args.label or default_label
    image = bytearray(fp.total_sectors * SECTOR)

    # 1. 부트섹터(기본 BPB) — boot 옵션이 없어도 BPB 는 항상 들어간다.
    image[:SECTOR] = make_boot_sector(fp, label, args.oem)

    # 2. 파일 배치
    next_cluster = 2
    chains: list[list[int]] = []
    dir_entries: list[bytes] = []

    for f in args.files:
        path = Path(f)
        data = path.read_bytes()
        cluster_bytes = fp.sectors_per_cluster * SECTOR
        cluster_count = max(1, (len(data) + cluster_bytes - 1) // cluster_bytes)
        chain = list(range(next_cluster, next_cluster + cluster_count))
        if chain[-1] >= fp.cluster_count + 2:
            return _die(f"디스크 가득 참: {f}")
        next_cluster += cluster_count
        chains.append(chain)

        # 데이터 영역에 쓰기
        for k, c in enumerate(chain):
            off = (fp.data_start_sector + (c - 2) * fp.sectors_per_cluster) * SECTOR
            chunk = data[k * cluster_bytes : (k + 1) * cluster_bytes]
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

    if len(dir_entries) > fp.root_entries:
        return _die(f"루트 엔트리 한계 초과 ({len(dir_entries)} > {fp.root_entries})")

    # 3. FAT (두 카피 동일하게 작성)
    fat = build_fat(fp, chains)
    for i in range(fp.fat_count):
        off = (fp.reserved + i * fp.sectors_per_fat) * SECTOR
        image[off : off + len(fat)] = fat

    # 4. 루트 디렉터리
    root_off = (fp.reserved + fp.fat_count * fp.sectors_per_fat) * SECTOR
    for i, e in enumerate(dir_entries):
        image[root_off + i * 32 : root_off + (i + 1) * 32] = e

    # 5. 부트섹터 오버라이드 (있을 때만; FAT16 데이터 디스크는 보통 생략)
    if args.boot:
        boot = Path(args.boot).read_bytes()
        if len(boot) > SECTOR:
            print(f"[warn] 부트섹터가 {len(boot)}바이트 — 처음 {SECTOR}바이트만 씀",
                  file=sys.stderr)
        image[: min(SECTOR, len(boot))] = boot[:SECTOR]

    Path(args.out).write_bytes(bytes(image))
    print(f"[mkfat12.py] wrote {args.out} "
          f"({fp.total_sectors * SECTOR // 1024} KB, {fp.fs}, "
          f"{len(args.files)} files, {fp.cluster_count} clusters)")
    return 0


def _die(msg: str) -> int:
    print(f"[error] {msg}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
