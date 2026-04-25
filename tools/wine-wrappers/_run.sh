#!/usr/bin/env bash
#
# z_tools/*.exe 를 macOS/Linux에서 Wine 으로 호출하기 위한 공용 래퍼.
# 심볼릭 링크로 호출됨: nask -> _run.sh, cc1 -> _run.sh ...
# 호출된 이름($0의 basename)을 그대로 *.exe 이름으로 사용해 z_tools/<name>.exe 를 wine으로 실행.
#
# 환경 변수:
#   BXOS_TOOLPATH    기본값: 자동 탐색 (이 스크립트 기준 ../../z_tools)
#   WINE             기본값: wine (Apple Silicon에서는 wine64 / wine-crossover 도 가능)
#   WINEDEBUG        기본값: -all  (잡소리 끄기)

set -euo pipefail

SELF_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_TOOLPATH="$(cd "$SCRIPT_DIR/../../z_tools" 2>/dev/null && pwd || true)"
TOOLPATH="${BXOS_TOOLPATH:-$DEFAULT_TOOLPATH}"

if [[ -z "$TOOLPATH" || ! -d "$TOOLPATH" ]]; then
  echo "[wine-wrap] z_tools 디렉터리를 찾을 수 없습니다 (BXOS_TOOLPATH 로 지정)" >&2
  exit 1
fi

EXE="$TOOLPATH/${SELF_NAME}.exe"
if [[ ! -f "$EXE" ]]; then
  echo "[wine-wrap] '$EXE' 가 없습니다." >&2
  exit 1
fi

WINE="${WINE:-wine}"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-bxos}"

if ! command -v "$WINE" >/dev/null 2>&1; then
  cat >&2 <<EOF
[wine-wrap] '$WINE' 를 찾을 수 없습니다.
  macOS Apple Silicon: brew install --cask --no-quarantine gcenx/wine/wine-crossover
                       또는 brew install --cask wine-stable (Rosetta 필요)
  macOS Intel:         brew install --cask wine-stable
  Linux:               sudo apt install wine
EOF
  exit 1
fi

# Wine 첫 실행 시 prefix 자동 생성. 잡소리 줄이기.
if [[ ! -d "$WINEPREFIX" ]]; then
  echo "[wine-wrap] 최초 실행: WINEPREFIX=$WINEPREFIX 초기화 중..." >&2
  "$WINE" wineboot --init >/dev/null 2>&1 || true
fi

exec "$WINE" "$EXE" "$@"
