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
  RESB <abs>-$       →  TIMES <abs-ORG>-($-$$) db 0

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
RX_ORG      = re.compile(r'^\s*ORG\s+([^\s;]+)', re.IGNORECASE)

# nask 는 RESB 가 $ 같은 라벨식을 받지만 NASM 의 RESB 는 .bss 용으로
# 즉시값만 받는다. flat binary 에서 패딩 목적인 경우 'TIMES <expr> db 0' 가
# 동등한 동작이므로 식이 들어간 RESB 만 골라 변환한다.
# 예: '		RESB	0x7dfe-$			; ...주석'
RX_RESB_EXPR = re.compile(
    r'^(\s*)RESB(\s+)(.+?)(\s*(?:;.*)?)$', re.IGNORECASE
)
RX_ABS_MINUS_DOLLAR = re.compile(r'^(0[xX][0-9a-fA-F]+|\d+)\s*-\s*\$$')

INSTRSET_MAP = {
    "i486p": "486",
    "i486":  "486",
    "i586p": "586",
    "i586":  "586",
    "i686p": "686",
    "i686":  "686",
    "p6":    "686",
}


def _parse_int_literal(s: str) -> int | None:
    if re.fullmatch(r'0[xX][0-9a-fA-F]+', s):
        return int(s, 16)
    if re.fullmatch(r'\d+', s):
        return int(s, 10)
    return None


def _is_nonascii_comment_line(line: str) -> bool:
    """
    nask 는 한국어/일본어 번역에서 ';' 가 빠진 채 본문이 시작되는 줄도
    묵시적으로 주석으로 받지만 NASM 은 그렇지 않다.
    공백을 걷어낸 첫 글자가 high-bit (>= 0x80) 면 주석으로 간주한다.
    이미 ';' 로 시작하는 줄은 영향받지 않음.
    """
    s = line.lstrip(" \t")
    if not s:
        return False
    if s[0] == ";":
        return False
    # latin-1 디코딩이라 첫 char 의 ord 가 그대로 byte 값
    return ord(s[0]) >= 0x80


def _convert_resb_expr(line: str, current_org: int | None) -> str | None:
    """
    'RESB <expr>' 에서 expr 가 단순 정수가 아니면 'TIMES <expr> db 0' 으로 치환.
    단순 정수(`RESB 18`, `RESB 0x10`)는 그대로 둔다.

    NASM 의 TIMES 인자는 ORG 가 섞인 '$' 표현식(예: 0x7dfe-$)을 absolute
    주소 차이로 계산하지 못한다. 이런 경우는 섹션 시작($$) 기준 offset 으로 바꾼다.
    """
    m = RX_RESB_EXPR.match(line)
    if not m:
        return None
    prefix, sep, expr, tail = m.groups()
    expr_stripped = expr.strip()
    # 16진/10진 단일 숫자면 그대로 둔다
    if re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|\d+)', expr_stripped):
        return None
    abs_target = RX_ABS_MINUS_DOLLAR.match(expr_stripped)
    if abs_target and current_org is not None:
        target = _parse_int_literal(abs_target.group(1))
        if target is not None:
            offset = target - current_org
            expr_stripped = f"0x{offset:x}-($-$$)"
    # 식이 들어있으면 TIMES 로 변환
    return f"{prefix}TIMES{sep}{expr_stripped} db 0{tail}\n"


def convert_line(line: str, current_org: int | None = None) -> str:
    if _is_nonascii_comment_line(line):
        # 라인 끝의 \n 을 살리되 앞에 ';' 를 끼워 NASM 에서 주석 처리되게 한다
        # (Latin-1 1:1 통과를 유지하기 위해 원본 그대로 보존)
        if line.endswith("\n"):
            return ";" + line
        return ";" + line + "\n"

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

    converted = _convert_resb_expr(line, current_org)
    if converted is not None:
        return converted

    return line


def convert(text: str) -> str:
    out = []
    current_org: int | None = None
    for ln in text.splitlines(keepends=True):
        m = RX_ORG.match(ln)
        if m:
            parsed_org = _parse_int_literal(m.group(1))
            if parsed_org is not None:
                current_org = parsed_org
        out.append(convert_line(ln, current_org))
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
