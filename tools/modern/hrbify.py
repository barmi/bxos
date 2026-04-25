#!/usr/bin/env python3
"""
hrbify.py — flat binary 에 32B HRB 헤더와 0x1B JMP를 박아 .hrb 로 바꿉니다.

HRB 헤더 레이아웃 (a.hrb 분석 결과):
    0x00 (4B)  stack_top    런타임 메모리 사용량 (스택 + heap 위쪽 끝)
    0x04 (4B)  "Hari"       매직 시그니처
    0x08 (4B)  malloc       추가 malloc 영역 크기
    0x0C (4B)  esp_init     초기 ESP (asmhead가 [EBX+12] 로 읽음)
    0x10 (4B)  data_size    별도 데이터 세그먼트 크기
    0x14 (4B)  data_file    데이터의 파일 오프셋
    0x18 (3B)  data_dest    데이터 세그먼트 적재 주소(커널 asmhead 에서는 미사용)
    0x1B (1B)  0xE9         JMP rel32 opcode  ← 실행은 여기서 시작
    0x1C (4B)  rel32        target = 0x20 + rel32
    0x20+      실제 코드, 그 뒤에 초기 데이터 이미지(linker 가 배치)

링커가 처음 32바이트는 0으로 비워두고, 0x20부터 실제 코드를 배치하도록
linker-*.lds 스크립트를 사용합니다. C 코드가 참조하는 상수/전역 데이터는
VMA 0x310000 으로 링크하고, Makefile 이 data_size/data_file 을 계산해
asmhead.nas 가 부팅 중 해당 데이터를 0x310000 으로 복사하게 합니다.

사용법:
    hrbify.py --in flat.bin --out file.hrb \\
              --stack-top SIZE --esp-init SIZE [--malloc SIZE] \\
              [--entry-offset OFFSET]

    --entry-offset 가 생략되면 0x20 (= 헤더 직후 첫 바이트) 으로 점프.
    이 경우 startup 코드가 0x20 에 와야 합니다.
"""

from __future__ import annotations
import argparse
import struct
import sys
from pathlib import Path


def parse_size(s: str) -> int:
    s = s.strip().lower()
    if s.endswith("k"):
        return int(s[:-1], 0) * 1024
    if s.endswith("m"):
        return int(s[:-1], 0) * 1024 * 1024
    return int(s, 0)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--in", dest="inp", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--stack-top", required=True, type=parse_size,
                   help="런타임 메모리 상한(=stack top). 예: 3136k")
    p.add_argument("--esp-init", required=True, type=parse_size,
                   help="초기 ESP. 보통 stack-top 과 같음")
    p.add_argument("--malloc", default="0", type=parse_size)
    p.add_argument("--data-size", default="0", type=parse_size)
    p.add_argument("--data-file", default="0", type=parse_size)
    p.add_argument("--data-dest", default="0", type=parse_size)
    p.add_argument("--entry-offset", default="0x20", type=lambda s: int(s, 0),
                   help="진입점 파일 오프셋 (기본 0x20)")
    args = p.parse_args(argv[1:])

    data = bytearray(Path(args.inp).read_bytes())
    if len(data) < 0x20:
        print(f"[hrbify] 입력이 너무 작음 ({len(data)} < 32B). "
              f"링커가 0x20 만큼 패딩을 두었는지 확인하세요.", file=sys.stderr)
        return 1

    # 헤더 채우기 (앞 0x20 바이트)
    struct.pack_into("<I", data, 0x00, args.stack_top)
    data[0x04:0x08] = b"Hari"
    struct.pack_into("<I", data, 0x08, args.malloc)
    struct.pack_into("<I", data, 0x0C, args.esp_init)
    struct.pack_into("<I", data, 0x10, args.data_size)
    struct.pack_into("<I", data, 0x14, args.data_file)

    # data_dest 의 저 24비트만 사용; 0x1B 는 JMP opcode 자리
    dd = args.data_dest & 0xFFFFFF
    data[0x18] = dd & 0xFF
    data[0x19] = (dd >> 8) & 0xFF
    data[0x1A] = (dd >> 16) & 0xFF

    data[0x1B] = 0xE9
    rel32 = (args.entry_offset - 0x20) & 0xFFFFFFFF
    struct.pack_into("<I", data, 0x1C, rel32)

    Path(args.out).write_bytes(bytes(data))
    print(f"[hrbify] {args.out}: {len(data)}B  "
          f"stack_top=0x{args.stack_top:X}  esp=0x{args.esp_init:X}  "
          f"entry=0x{args.entry_offset:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
