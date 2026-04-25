#!/usr/bin/env python3
"""
he2pack.py — Convert a linked HE2 ELF executable into a flat .he2 file.

기본 동작:
  - input ELF 의 모든 PROGBITS 섹션을 file offset 순서로 직렬화한다.
    (.he2_header → .text → .rodata → .data)
  - 결과의 첫 32B 가 HE2 magic("HE2\0") 으로 시작하는지 검증.
  - 결과의 image_size 필드가 실제 출력 길이와 일치하는지 검증.
  - 헤더 정보 (entry_off, image_size, bss_size, stack_size, heap_size) 를
    stderr 에 출력해 디버깅을 돕는다.

링커가 모든 오프셋/사이즈를 채워주므로 이 도구는 *패치* 가 아니라
*검증* 만 한다 — 신뢰할 수 있는 산출물을 보장하기 위함.

사용법:
    he2pack.py --in path/to/app.elf --out path/to/app.he2
    he2pack.py --in app.elf --out app.he2 --info-only
"""

from __future__ import annotations
import argparse
import struct
import subprocess
import sys
import shutil
import os
from pathlib import Path


HE2_MAGIC = b"HE2\x00"
HE2_HEADER_FMT = "<4sHHIIIIII"   # magic, ver, hdr_size, entry, image, bss, stack, heap, flags
HE2_HEADER_SIZE = 32


def find_objcopy() -> str:
    for cand in ("i686-elf-objcopy", "x86_64-elf-objcopy", "objcopy"):
        if shutil.which(cand):
            return cand
    print("error: no objcopy found (i686-elf-objcopy / x86_64-elf-objcopy / objcopy)",
          file=sys.stderr)
    sys.exit(2)


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--in", dest="elf", required=True, help="input ELF (linked HE2 image)")
    p.add_argument("--out", dest="out", help="output .he2 file (omit with --info-only)")
    p.add_argument("--objcopy", default=None, help="explicit objcopy binary")
    p.add_argument("--flags", default=None, help="override HE2 flags field")
    p.add_argument("--info-only", action="store_true", help="don't write output, just print header")
    args = p.parse_args(argv[1:])

    elf_path = Path(args.elf)
    if not elf_path.exists():
        print(f"error: input not found: {elf_path}", file=sys.stderr)
        return 2

    objcopy = args.objcopy or find_objcopy()

    # objcopy --only-section 로 PROGBITS 만 직렬화 (.bss 등 NOLOAD 는 자동 제외)
    out_path = Path(args.out) if args.out else Path(args.elf).with_suffix(".he2.tmp")
    cmd = [objcopy, "-O", "binary",
           "--remove-section=.eh_frame",
           "--remove-section=.comment",
           "--remove-section=.note*",
           str(elf_path), str(out_path)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"error: objcopy failed:\n{res.stderr}", file=sys.stderr)
        return res.returncode

    data = bytearray(out_path.read_bytes())
    if len(data) < HE2_HEADER_SIZE:
        print(f"error: output too small ({len(data)}B < {HE2_HEADER_SIZE}B)", file=sys.stderr)
        return 1

    magic, ver, hdr_size, entry, image, bss, stack, heap, flags = struct.unpack(
        HE2_HEADER_FMT, data[:HE2_HEADER_SIZE])

    if magic != HE2_MAGIC:
        print(f"error: bad HE2 magic: got {magic!r}, expected {HE2_MAGIC!r}", file=sys.stderr)
        return 1
    if ver != 1:
        print(f"warn: unexpected HE2 version: {ver}", file=sys.stderr)
    if hdr_size != HE2_HEADER_SIZE:
        print(f"warn: header_size {hdr_size} != {HE2_HEADER_SIZE}", file=sys.stderr)
    if image != len(data):
        print(f"error: image_size in header ({image}) != actual file size ({len(data)})",
              file=sys.stderr)
        return 1
    if entry < HE2_HEADER_SIZE or entry >= image:
        print(f"error: entry_off out of range: {entry:#x} (image={image:#x})",
              file=sys.stderr)
        return 1

    if args.flags is not None:
        flags = int(args.flags, 0)
        struct.pack_into("<I", data, 0x1C, flags)
        out_path.write_bytes(data)

    subsystem = "window" if (flags & 0x3) == 1 else "console"
    print(f"[he2pack] {out_path.name}: {len(data)}B  "
          f"entry=0x{entry:X}  image=0x{image:X}  bss=0x{bss:X}  "
          f"stack=0x{stack:X}  heap=0x{heap:X}  flags=0x{flags:X}  "
          f"subsystem={subsystem}",
          file=sys.stderr)

    if args.info_only:
        if not args.out:
            os.unlink(out_path)
        return 0

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
