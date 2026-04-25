#!/usr/bin/env bash
#
# bxos / haribote OS QEMU 실행 스크립트 (macOS · Linux 공통)
#
# 사용법:
#   ./run-qemu.sh                  # harib27f/haribote.img 를 플로피로 부팅
#   ./run-qemu.sh iso              # harib27f/haribote.iso 를 CD로 부팅
#   ./run-qemu.sh path/to/foo.img  # 임의 이미지를 플로피로 부팅
#   QEMU_EXTRA="-d int" ./run-qemu.sh   # 추가 옵션
#
# 환경 변수:
#   QEMU_BIN      기본값 qemu-system-i386
#   QEMU_MEM      기본값 32 (MB)  ← 책 기준 32MB. 부족하면 64/128 등으로
#   QEMU_EXTRA    추가 인자
#   QEMU_ACCEL    기본값 tcg (Apple Silicon에서 i386 HVF는 미지원)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU_BIN="${QEMU_BIN:-qemu-system-i386}"
QEMU_MEM="${QEMU_MEM:-32}"
QEMU_ACCEL="${QEMU_ACCEL:-tcg}"
QEMU_EXTRA="${QEMU_EXTRA:-}"

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  cat >&2 <<EOF
[error] '$QEMU_BIN' 를 찾을 수 없습니다.
  macOS:    brew install qemu
  Ubuntu:   sudo apt install qemu-system-x86
EOF
  exit 1
fi

ARG="${1:-fda}"

case "$ARG" in
  ""|fda|floppy)
    IMG="$SCRIPT_DIR/harib27f/haribote.img"
    BOOT_OPTS=(-fda "$IMG" -boot a)
    KIND="floppy"
    ;;
  iso|cd|cdrom)
    IMG="$SCRIPT_DIR/harib27f/haribote.iso"
    BOOT_OPTS=(-cdrom "$IMG" -boot d)
    KIND="cdrom"
    ;;
  *)
    IMG="$ARG"
    if [[ "$IMG" == *.iso ]]; then
      BOOT_OPTS=(-cdrom "$IMG" -boot d)
      KIND="cdrom"
    else
      BOOT_OPTS=(-fda "$IMG" -boot a)
      KIND="floppy"
    fi
    ;;
esac

if [[ ! -f "$IMG" ]]; then
  echo "[error] 이미지 파일이 없습니다: $IMG" >&2
  exit 1
fi

echo "[info] $KIND boot: $IMG"
echo "[info] qemu: $QEMU_BIN  mem=${QEMU_MEM}M  accel=$QEMU_ACCEL"

# shellcheck disable=SC2086
exec "$QEMU_BIN" \
  -m "$QEMU_MEM" \
  -accel "$QEMU_ACCEL" \
  -rtc base=localtime \
  -name "BxOS / Haribote" \
  "${BOOT_OPTS[@]}" \
  $QEMU_EXTRA
