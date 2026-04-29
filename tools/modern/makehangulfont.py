#!/usr/bin/env python3
"""
makehangulfont.py — Hangul 16x16 폰트 + EUC-KR 매핑 헤더 생성.

입력:
  --bdf <unifont-hangul.bdf>   Unifont 의 Hangul 영역 (U+AC00..U+D7A3) 추출본.
                               전체 11172 음절을 모두 포함해야 한다.
  --ascii <hankaku.bin>        4096바이트 ASCII 폰트 (makefont.py 의 출력).

출력:
  --out-fnt <hangul.fnt>       바이너리 폰트.
                               레이아웃: 0..4095   = ASCII (hankaku 그대로),
                                         4096..   = 32바이트씩 음절 글리프,
                                         idx = (codepoint - 0xAC00), 0..11171.
                                         총 4096 + 32*11172 = 361600 bytes.
  --out-map <euckr_map.h>      EUC-KR → Unicode syllable 매핑 C 헤더.
                               94*94 unsigned short 배열, 매핑 없으면 0xFFFF.

설계:
  - BDF 의 BBX 가 16x16 가 아니어도 16x16 셀에 좌측 상단 정렬로 클립/패드.
  - BDF 누락 음절 슬롯은 0xFF 32바이트 (tofu) — Unifont 는 11172 음절을
    모두 가지고 있으므로 정상 입력에서는 발생하지 않는다.
  - 결정론적 — 동일 입력에서 byte-identical 결과를 보장한다 (set/dict 정렬 무관).

사용:
  ./makehangulfont.py \
      --bdf  ../../harib27f/hangul/unifont-hangul.bdf \
      --ascii ../../build/cmake/kernel/hankaku.bin \
      --out-fnt ../../harib27f/hangul/hangul.fnt \
      --out-map ./euckr_map.h
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

HANGUL_FIRST = 0xAC00
HANGUL_LAST  = 0xD7A3
HANGUL_COUNT = HANGUL_LAST - HANGUL_FIRST + 1   # 11172

GLYPH_BYTES  = 32     # 16 rows * 16 cols / 8 bits = 32 bytes
ASCII_BYTES  = 4096   # 256 glyphs * 16 bytes
FNT_TOTAL    = ASCII_BYTES + GLYPH_BYTES * HANGUL_COUNT  # 361600


def parse_bdf(path: Path) -> dict[int, bytes]:
    """Return {codepoint: 32-byte glyph} for U+AC00..U+D7A3 only.

    Each glyph is 16 rows of 2 bytes (left half + right half) — exactly the
    layout the kernel's putfonts8_asc expects: putfont8(left) then putfont8(right+16).
    BBX < 16x16 is left-top aligned and zero-padded; BBX > 16x16 is clipped
    (Unifont Hangul is 16x16 by construction so neither path normally fires).
    """
    glyphs: dict[int, bytes] = {}
    enc: int | None = None
    bbx_w = bbx_h = bbx_xoff = bbx_yoff = 0
    in_bitmap = False
    bitmap_rows: list[int] = []  # raw integer per row, MSB-aligned to 16 bits

    with path.open("rb") as f:
        for raw in f:
            line = raw.decode("latin-1").rstrip()
            if line.startswith("ENCODING "):
                try:
                    enc = int(line.split()[1])
                except ValueError:
                    enc = None
            elif line.startswith("BBX "):
                parts = line.split()
                bbx_w   = int(parts[1])
                bbx_h   = int(parts[2])
                bbx_xoff = int(parts[3])
                bbx_yoff = int(parts[4])
            elif line.startswith("BITMAP"):
                in_bitmap = True
                bitmap_rows = []
            elif line.startswith("ENDCHAR"):
                if enc is not None and HANGUL_FIRST <= enc <= HANGUL_LAST:
                    glyphs[enc] = _bdf_to_glyph(bitmap_rows, bbx_w, bbx_h, bbx_xoff, bbx_yoff)
                in_bitmap = False
                enc = None
                bitmap_rows = []
            elif in_bitmap and line:
                # BDF bitmap row hex; padded to ceil(width/8)*2 hex digits.
                # We treat each row as a big-endian integer of (ceil(width/8)*8) bits,
                # MSB at left of cell.
                # Strip optional spaces.
                hex_row = line.strip()
                row_bytes = len(hex_row) // 2
                value = int(hex_row, 16)
                # Left-justify to 16 columns.
                if row_bytes < 2:
                    value <<= (16 - row_bytes * 8)
                elif row_bytes > 2:
                    value >>= (row_bytes * 8 - 16)
                bitmap_rows.append(value & 0xFFFF)
    return glyphs


def _bdf_to_glyph(rows: list[int], w: int, h: int, xoff: int, yoff: int) -> bytes:
    """Render BDF rows into a 16x16 glyph (32 bytes).

    Layout matches the kernel's putfonts8_asc rendering of full-width glyphs:
    bytes  0..15 = left  half (16 rows of cols 0..7,  one byte per row),
    bytes 16..31 = right half (16 rows of cols 8..15, one byte per row).
    putfont8(font) draws the left half, putfont8(font + 16) draws the right.
    """
    cell = bytearray(GLYPH_BYTES)
    for i, row in enumerate(rows[:16]):
        # row is already MSB-aligned to 16 bits. Apply x offset by shifting.
        if xoff > 0:
            row >>= xoff
        elif xoff < 0:
            row <<= -xoff
        row &= 0xFFFF
        cell[i]      = (row >> 8) & 0xFF   # left  half row i (cols 0..7)
        cell[16 + i] = row & 0xFF          # right half row i (cols 8..15)
    return bytes(cell)


def build_euckr_map() -> list[int]:
    """Return 94*94 list of (Unicode syllable codepoint or 0xFFFF).

    Index = (lead - 0xA1)*94 + (trail - 0xA1) for lead/trail in 0xA1..0xFE.
    Only KSX1001 entries that decode to U+AC00..U+D7A3 are kept; everything
    else (Hanja, symbols, undefined, non-Hangul) is 0xFFFF — the kernel falls
    back to hankaku for those.
    """
    table = [0xFFFF] * (94 * 94)
    for lead in range(0xA1, 0xFF):
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes([lead, trail]).decode("euc_kr")
            except UnicodeDecodeError:
                continue
            if len(ch) != 1:
                continue
            cp = ord(ch)
            if HANGUL_FIRST <= cp <= HANGUL_LAST:
                idx = (lead - 0xA1) * 94 + (trail - 0xA1)
                table[idx] = cp
    return table


def write_fnt(out_path: Path, ascii_bytes: bytes, glyphs: dict[int, bytes]) -> None:
    if len(ascii_bytes) != ASCII_BYTES:
        raise SystemExit(f"--ascii must be exactly {ASCII_BYTES} bytes (got {len(ascii_bytes)})")
    buf = bytearray(FNT_TOTAL)
    buf[0:ASCII_BYTES] = ascii_bytes
    tofu = b"\xFF" * GLYPH_BYTES
    missing = 0
    for cp in range(HANGUL_FIRST, HANGUL_LAST + 1):
        idx = cp - HANGUL_FIRST
        off = ASCII_BYTES + idx * GLYPH_BYTES
        glyph = glyphs.get(cp)
        if glyph is None:
            buf[off:off + GLYPH_BYTES] = tofu
            missing += 1
        else:
            buf[off:off + GLYPH_BYTES] = glyph
    out_path.write_bytes(bytes(buf))
    if missing:
        print(f"  warn: {missing} Hangul syllables missing from BDF (filled with 0xFF tofu)",
              file=sys.stderr)


def write_map_header(out_path: Path, table: list[int]) -> None:
    """Emit a C header with a 94*94 unsigned short array."""
    n = len(table)
    lines = [
        "/* Auto-generated by tools/modern/makehangulfont.py.  Do not edit. */",
        "/* EUC-KR (KS X 1001) -> Unicode Hangul syllable codepoint. */",
        "/* Index = (lead - 0xA1) * 94 + (trail - 0xA1). 0xFFFF = no Hangul mapping. */",
        "#ifndef BXOS_EUCKR_MAP_H",
        "#define BXOS_EUCKR_MAP_H",
        "",
        f"#define BXOS_EUCKR_MAP_LEN {n}",
        "static const unsigned short g_euckr_to_uhs[BXOS_EUCKR_MAP_LEN] = {",
    ]
    # 8 entries per line for compactness.
    for i in range(0, n, 8):
        chunk = table[i:i + 8]
        lines.append("\t" + ", ".join(f"0x{v:04X}" for v in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* BXOS_EUCKR_MAP_H */")
    out_path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    p = argparse.ArgumentParser(description="Build hangul.fnt + euckr_map.h.")
    p.add_argument("--bdf",     required=True, type=Path, help="Unifont Hangul BDF subset")
    p.add_argument("--ascii",   required=True, type=Path, help="hankaku.bin (4096 bytes)")
    p.add_argument("--out-fnt", required=True, type=Path, help="output hangul.fnt")
    p.add_argument("--out-map", required=True, type=Path, help="output euckr_map.h")
    args = p.parse_args()

    print(f"[makehangulfont] BDF: {args.bdf}", file=sys.stderr)
    glyphs = parse_bdf(args.bdf)
    print(f"  parsed {len(glyphs)} Hangul glyphs", file=sys.stderr)
    if len(glyphs) != HANGUL_COUNT:
        print(f"  warn: expected {HANGUL_COUNT}, got {len(glyphs)}", file=sys.stderr)

    ascii_bytes = args.ascii.read_bytes()
    print(f"  ASCII: {len(ascii_bytes)} bytes", file=sys.stderr)

    write_fnt(args.out_fnt, ascii_bytes, glyphs)
    print(f"  -> {args.out_fnt}  ({args.out_fnt.stat().st_size} bytes)", file=sys.stderr)

    table = build_euckr_map()
    mapped = sum(1 for v in table if v != 0xFFFF)
    print(f"  EUC-KR -> Hangul mappings: {mapped}", file=sys.stderr)
    write_map_header(args.out_map, table)
    print(f"  -> {args.out_map}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
