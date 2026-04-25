#!/usr/bin/env bash
#
# tools/wine-wrappers/_run.sh 를 가리키는 심볼릭 링크를 만들어
# Makefile 안의 `$(TOOLPATH)nask.exe` 같은 호출을 wine 호출로 매핑할 때
# `make NASK=../tools/wine-wrappers/nask` 식으로 사용 가능.
# 또는 build-mac.sh 가 PATH 앞에 이 디렉터리를 끼워 넣어 자동으로 잡습니다.

set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

TOOLS=(nask cc1 gas2nask obj2bim bim2hrb bim2bin makefont bin2obj
       edimg golib00 cpp0 aska aksa naskcnv0 sjisconv sartol osalink1
       wce ld upx t5lzma make doscmd comcom)

for t in "${TOOLS[@]}"; do
  ln -sfv _run.sh "$t"
done

echo "[ok] $(ls | grep -v _run.sh | grep -v install-symlinks.sh | wc -l) 개의 wrapper 심볼릭 링크 생성 완료."
