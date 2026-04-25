#!/usr/bin/env python3
"""
nas2nasm.py — nask(.nas) 소스를 NASM 호환으로 패치해서 표준 출력 또는 파일로 씁니다.

원본 *.nas 파일은 건드리지 않고, 변환 결과를 다른 위치에 출력합니다.

처리 규칙:
  [FORMAT "WCOFF"]   →  ; (제거; -f elf32 등 명령행 옵션으로 대체)
  [INSTRSET "..."]   →  CPU <NN>   (i486p → CPU 486 등)
  [FILE "..."]       →  ; (제거; nask 디버그 정보용, NASM에는 불필요)
  [BITS 32]          →  BITS 32    (NASM도 [BITS 32] 허용하지만 정규화)
  [SECTION .text]    →  SECTION .text

주석/문자열 안의 'INSTRSET'/'FORMAT' 등은 건드리지 않습니다.

사용법:
    nas2nasm.py <input.nas> [output.nas]
        output 생략 시 표준출력으로.
"""

from __future__ import annotations
import re
import sys
from pathlib import Path

# 줄 시작에 [DIRECTIVE ...] 형태가 오는 경우만 잡는다.
# 닫는 ']' 뒤로는 nask 식 ; 주석이 따라올 수 있으므로 줄끝 매칭은 느슨하게.
TAIL = r'\s*(?:;.*)?$'
RX_FORMAT   = re.compile(r'^\s*\[\s*FORMAT\s+"[^"]+"\s*\]' + TAIL, re.IGNORECASE)
RX_FILE     = re.compile(r'^\s*\[\s*FILE\s+"[^"]+"\s*\]' + TAIL, re.IGNORECASE)
RX_INSTRSET = re.compile(r'^\s*\[\s*INSTRSET\s+"([^"]+)"\s*\]' + TAIL, re.IGNORECASE)
RX_BITS     = re.compile(r'^\s*\[\s*BITS\s+(\d+)\s*\]' + TAIL, re.IGNORECASE)
RX_SECTION  = re.compile(r'^\s*\[\s*SECTION\s+([^\];]+?)\s*\]' + TAIL, re.IGNORECASE)

INSTRSET_MAP = {
    "i486p": "486",
    "i486":  "486",
    "i586p": "586",
    "i586":  "586",
    "i686p": "686",
    "i686":  "686",
    "p6":    "686",
}


def convert_line(line: str) -> str:
    if RX_FORMAT.match(line):
        return f"; [nas2nasm] removed: {line.rstrip()}\n"
    if RX_FILE.match(line):
        return f"; [nas2nasm] removed: {line.rstrip()}\n"
    m = RX_INSTRSET.match(line)
    if m:
        cpu = INSTRSET_MAP.get(m.group(1).lower(), "486")
        return f"CPU {cpu}\t; [nas2nasm] from {line.rstrip()}\n"
    m = RX_BITS.match(line)
    if m:
        return f"BITS {m.group(1)}\n"
    m = RX_SECTION.match(line)
    if m:
        return f"SECTION {m.group(1).strip()}\n"
    return line


def convert(text: str) -> str:
    out = []
    for ln in text.splitlines(keepends=True):
        out.append(convert_line(ln))
    return "".join(out)


def main(argv: list[str]) -> int:
    if not 2 <= len(argv) <= 3:
        print("usage: nas2nasm.py <input.nas> [output.nas]", file=sys.stderr)
        return 2
    src = Path(argv[1]).read_bytes()
    # nask 소스가 EUC-KR/SJIS 등으로 저장되어도 디렉티브들은 ASCII 영역이라 안전.
    # 잘못된 멀티바이트가 줄 안에 섞여 있을 수 있으니 latin-1로 1:1 통과시킨다.
    text = src.decode("latin-1")
    out_text = convert(text)
    out_bytes = out_text.encode("latin-1")
    if len(argv) == 3:
        Path(argv[2]).write_bytes(out_bytes)
    else:
        sys.stdout.buffer.write(out_bytes)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
