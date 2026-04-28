#!/usr/bin/env bash
#
# bxos / haribote OS QEMU 실행 스크립트 (macOS · Linux 공통)
#
# 사용법:
#   ./run-qemu.sh                       # build/cmake/haribote.img 를 플로피로 부팅
#                                       #   (없으면 harib27f/haribote.img 폴백)
#                                       #   build/cmake/data.img 가 있으면 -hda 로 자동 부착
#   ./run-qemu.sh iso                   # haribote.iso 를 CD로 부팅
#   ./run-qemu.sh path/to/foo.img       # 임의 부팅 이미지
#   ./run-qemu.sh --data path/to/data.img
#                                       # 데이터 디스크 명시 (-hda 로 부착)
#   ./run-qemu.sh --no-data             # 자동 부착된 data.img 무시
#   ./run-qemu.sh path.img --data d.img # 부팅 이미지 + 데이터 디스크 동시 지정
#   QEMU_EXTRA="-d int" ./run-qemu.sh   # 추가 옵션
#
# 환경 변수:
#   QEMU_BIN       기본값 qemu-system-i386
#   QEMU_MEM       기본값 32 (MB)  ← 책 기준 32MB. 부족하면 64/128 등으로
#   QEMU_EXTRA     추가 인자
#   QEMU_ACCEL     기본값 tcg (Apple Silicon에서 i386 HVF는 미지원)
#   BXOS_DATA_IMG  데이터 디스크 경로. 비어 있고 기본 경로(build/cmake/data.img)
#                  가 존재하면 자동 부착. --data / --no-data 가 우선.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU_BIN="${QEMU_BIN:-qemu-system-i386}"
QEMU_MEM="${QEMU_MEM:-32}"
QEMU_ACCEL="${QEMU_ACCEL:-tcg}"
QEMU_EXTRA="${QEMU_EXTRA:-}"

DEFAULT_KERNEL_IMG="$SCRIPT_DIR/build/cmake/haribote.img"
LEGACY_KERNEL_IMG="$SCRIPT_DIR/harib27f/haribote.img"
DEFAULT_DATA_IMG="$SCRIPT_DIR/build/cmake/data.img"

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  cat >&2 <<EOF
[error] '$QEMU_BIN' 를 찾을 수 없습니다.
  macOS:    brew install qemu
  Ubuntu:   sudo apt install qemu-system-x86
EOF
  exit 1
fi

# ── 인자 파싱 ──────────────────────────────────────────────────────
BOOT_ARG=""
DATA_IMG="${BXOS_DATA_IMG:-}"
DATA_OVERRIDE="auto"   # auto | explicit | disabled

while [[ $# -gt 0 ]]; do
  case "$1" in
    --data)
      DATA_IMG="${2:-}"
      DATA_OVERRIDE="explicit"
      shift 2
      ;;
    --data=*)
      DATA_IMG="${1#--data=}"
      DATA_OVERRIDE="explicit"
      shift
      ;;
    --no-data)
      DATA_IMG=""
      DATA_OVERRIDE="disabled"
      shift
      ;;
    -h|--help)
      sed -n '2,30p' "$0" >&2
      exit 0
      ;;
    *)
      if [[ -z "$BOOT_ARG" ]]; then
        BOOT_ARG="$1"
      else
        echo "[error] 알 수 없는 인자: $1" >&2
        exit 1
      fi
      shift
      ;;
  esac
done

ARG="${BOOT_ARG:-fda}"

case "$ARG" in
  fda|floppy)
    if [[ -f "$DEFAULT_KERNEL_IMG" ]]; then
      IMG="$DEFAULT_KERNEL_IMG"
    else
      IMG="$LEGACY_KERNEL_IMG"
    fi
    BOOT_OPTS=(-fda "$IMG" -boot a)
    KIND="floppy"
    ;;
  iso|cd|cdrom)
    if [[ -f "$SCRIPT_DIR/build/cmake/haribote.iso" ]]; then
      IMG="$SCRIPT_DIR/build/cmake/haribote.iso"
    else
      IMG="$SCRIPT_DIR/harib27f/haribote.iso"
    fi
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
  echo "[error] 부팅 이미지 파일이 없습니다: $IMG" >&2
  exit 1
fi

# ── 데이터 디스크 자동 부착 ─────────────────────────────────────────
if [[ "$DATA_OVERRIDE" == "auto" && -z "$DATA_IMG" && -f "$DEFAULT_DATA_IMG" ]]; then
  DATA_IMG="$DEFAULT_DATA_IMG"
fi

DATA_OPTS=()
if [[ -n "$DATA_IMG" ]]; then
  if [[ ! -f "$DATA_IMG" ]]; then
    echo "[error] 데이터 디스크 파일이 없습니다: $DATA_IMG" >&2
    exit 1
  fi
  DATA_OPTS=(-hda "$DATA_IMG")
fi

# ── 실행 ───────────────────────────────────────────────────────────
echo "[info] $KIND boot : $IMG"
if [[ ${#DATA_OPTS[@]+x} && ${#DATA_OPTS[@]} -gt 0 ]]; then
  echo "[info] data disk : $DATA_IMG (-hda)"
else
  echo "[info] data disk : (none)"
fi
echo "[info] qemu      : $QEMU_BIN  mem=${QEMU_MEM}M  accel=$QEMU_ACCEL"

# shellcheck disable=SC2086
exec "$QEMU_BIN" \
  -m "$QEMU_MEM" \
  -accel "$QEMU_ACCEL" \
  -rtc base=localtime \
  -name "BxOS / Haribote" \
  "${BOOT_OPTS[@]}" \
  ${DATA_OPTS[@]+"${DATA_OPTS[@]}"} \
  $QEMU_EXTRA
