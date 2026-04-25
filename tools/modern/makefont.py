#!/usr/bin/env python3
"""
makefont.py — z_tools/makefont.exe 의 순수 Python 대체.

hankaku.txt 형식(8x16 픽셀, '.'=0, '*'=1)을 256문자분 = 4096바이트 바이너리로 변환.

사용법:
    makefont.py <input.txt> <output.bin>

원본은 EUC-KR/SJIS 헤더가 들어있을 수 있으나 'char 0xNN' 라인부터만 의미가 있으므로
바이트 읽기로 처리해 인코딩 영향을 받지 않게 했습니다.
"""

from __future__ import annotations
import sys
import re
from pathlib import Path

CHAR_RE = re.compile(rb"^\s*char\s+0x([0-9a-fA-F]+)\s*$")


def parse(text_bytes: bytes) -> bytes:
    """Return 256*16 = 4096 byte font table."""
    table = bytearray(4096)  # zero-filled
    lines = text_bytes.splitlines()
    i = 0
    n = len(lines)
    while i < n:
        m = CHAR_RE.match(lines[i])
        if not m:
            i += 1
            continue
        idx = int(m.group(1), 16)
        if idx > 0xFF:
            raise ValueError(f"char index out of range: 0x{idx:X}")
        i += 1
        # 다음 16줄이 8픽셀짜리 행
        rows: list[int] = []
        while i < n and len(rows) < 16:
            line = lines[i].rstrip(b"\r\n")
            if not line.strip() and not rows:
                # 첫 빈 줄 무시
                i += 1
                continue
            # '.' / '*' 가 아닌 행이 나오면 종료
            stripped = line.strip()
            if not stripped or not all(c in b".*" for c in stripped):
                break
            if len(stripped) != 8:
                raise ValueError(
                    f"char 0x{idx:02X}: 픽셀 행은 8자리여야 합니다 (받은: {stripped!r})"
                )
            byte = 0
            for bit, ch in enumerate(stripped):
                if ch == ord("*"):
                    byte |= 1 << (7 - bit)
            rows.append(byte)
            i += 1
        if len(rows) != 16:
            raise ValueError(
                f"char 0x{idx:02X}: 16행이 필요한데 {len(rows)}행만 읽었습니다."
            )
        base = idx * 16
        table[base : base + 16] = bytes(rows)
    return bytes(table)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: makefont.py <input.txt> <output.bin>", file=sys.stderr)
        return 2
    in_path, out_path = Path(argv[1]), Path(argv[2])
    data = parse(in_path.read_bytes())
    out_path.write_bytes(data)
    print(f"[makefont.py] wrote {out_path} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
