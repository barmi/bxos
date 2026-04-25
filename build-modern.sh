#!/usr/bin/env bash
#
# build-modern.sh — Wine 없이 NASM + i686-elf-gcc + Python 만으로 BxOS 커널 빌드.
#
# 사용법:
#   ./build-modern.sh           # 전체 (haribote.img 까지)
#   ./build-modern.sh kernel    # 커널만 (bootpack.hrb)
#   ./build-modern.sh info      # 도구/경로 점검
#   ./build-modern.sh clean
#
# 사전 설치 (macOS Apple Silicon):
#   brew install nasm i686-elf-gcc i686-elf-binutils qemu
# 단, brew 의 i686-elf-* 패키지가 없는 환경(요즘 brew 정책)이라면:
#   brew install nasm x86_64-elf-gcc x86_64-elf-binutils qemu
#   ─ Makefile.modern 가 자동으로 x86_64-elf-* 로 폴백하고 -m32 를 사용합니다.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MK="$ROOT/tools/modern/Makefile.modern"

if [[ ! -f "$MK" ]]; then
  echo "[error] Makefile.modern 가 없습니다: $MK" >&2
  exit 1
fi

# 도구 점검 (정보용 — 실패 시 make 가 알아서 에러 냄)
need=()
for t in nasm python3; do
  command -v "$t" >/dev/null 2>&1 || need+=("$t")
done
if ! command -v i686-elf-gcc >/dev/null 2>&1 \
   && ! command -v x86_64-elf-gcc >/dev/null 2>&1; then
  need+=("(i686-elf-gcc 또는 x86_64-elf-gcc)")
fi
if [[ ${#need[@]} -gt 0 ]]; then
  echo "[warn] 다음 도구가 PATH에 없습니다: ${need[*]}" >&2
  echo "       설치 후 다시 실행하세요. (brew install nasm i686-elf-gcc i686-elf-binutils qemu)" >&2
fi

TARGET="${1:-all}"
case "$TARGET" in
  ""|all)    exec make -f "$MK" all ;;
  kernel)    exec make -f "$MK" kernel ;;
  info)      exec make -f "$MK" info ;;
  clean)     exec make -f "$MK" clean ;;
  *)         exec make -f "$MK" "$TARGET" ;;
esac
