#!/usr/bin/env bash
#
# cmake/finalize-bootpack-hrb.sh
#
# bootpack.elf 의 __hrb_data_start/end/lma 심볼에서 데이터 영역 정보를 뽑아
# hrbify.py 로 bootpack.bin 에 HRB 헤더를 부착해 bootpack.hrb 를 만든다.
#
# Makefile.modern 의 bootpack.hrb 레시피와 동일한 동작이지만, CMake 가 만든
# Makefile 에서 multi-line shell 을 안전하게 표현하기 위해 별도 스크립트로 분리.
#
# 사용:
#   finalize-bootpack-hrb.sh <NM> <PYTHON> <HRBIFY> <BOOTPACK_ELF> <BOOTPACK_BIN> <BOOTPACK_HRB>

set -euo pipefail

if [[ $# -ne 6 ]]; then
    echo "usage: $0 <NM> <PYTHON> <HRBIFY> <BOOTPACK_ELF> <BOOTPACK_BIN> <BOOTPACK_HRB>" >&2
    exit 64
fi

NM=$1
PY=$2
HRBIFY=$3
ELF=$4
BIN=$5
HRB=$6

ds=0x$("$NM" "$ELF" | awk '$3=="__hrb_data_start"{print $1}')
de=0x$("$NM" "$ELF" | awk '$3=="__hrb_data_end"{print $1}')
dl=0x$("$NM" "$ELF" | awk '$3=="__hrb_data_lma"{print $1}')
dz=$((de - ds))
st=$(((ds + dz + 0xfff) & ~0xfff))

"$PY" "$HRBIFY" --in "$BIN" --out "$HRB" \
    --stack-top "$st" --esp-init "$ds" --malloc 0 \
    --data-size "$dz" --data-file "$dl"
