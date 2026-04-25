#!/usr/bin/env bash
#
# bxos / Haribote OS 를 macOS(또는 Linux)에서 Wine 으로 빌드.
#
# 사용법:
#   ./build-mac.sh                # harib27f 전체를 빌드(haribote.img 생성)
#   ./build-mac.sh haribote/      # 커널만
#   ./build-mac.sh app a/         # 특정 앱(a)만
#   ./build-mac.sh clean          # 청소
#
# 동작:
#   1) tools/wine-wrappers/ 의 심볼릭 링크를 생성(없으면).
#   2) PATH 에 wrapper 디렉터리를 앞쪽에 끼워 넣어, Makefile 내부의
#      `$(TOOLPATH)nask.exe`, `cc1.exe`, … 호출이 모두 wine 으로 위임되게 함.
#   3) Windows 의 `copy`/`del` 을 흉내내는 셸 함수를 PATH 에 노출.
#   4) make.exe 대신 시스템 GNU make 를 사용하되, 원본 Makefile 의 변수만
#      override 해서(MAKE/COPY/DEL/$(TOOLPATH)cc1.exe …) 실제 동작을 바꿉니다.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WRAPPERS="$ROOT/tools/wine-wrappers"
SHIM="$ROOT/tools/shim"
TARGET="${1:-harib27f}"

# (1) wrapper 심볼릭 링크
if [[ ! -L "$WRAPPERS/nask" ]]; then
  ( cd "$WRAPPERS" && bash install-symlinks.sh )
fi

# (2)/(3) shim 디렉터리에 copy / del 가짜 명령 만들기
mkdir -p "$SHIM"
cat > "$SHIM/copy" <<'EOF'
#!/usr/bin/env bash
# DOS 'copy' 흉내. 인자가 1개면 stdout 으로, 2개면 cp.
if [[ $# -eq 2 ]]; then exec cp -f "$1" "$2"; fi
exec cat "$@"
EOF
cat > "$SHIM/del" <<'EOF'
#!/usr/bin/env bash
exec rm -f "$@"
EOF
chmod +x "$SHIM/copy" "$SHIM/del"

# (4) PATH 셋업
export PATH="$WRAPPERS:$SHIM:$PATH"

echo "[build] PATH preview: $WRAPPERS:$SHIM:..."
echo "[build] target: $TARGET"

# Makefile 변수 override.
# 원본은 $(TOOLPATH)nask.exe 형태로 절대 경로를 박아 호출하는데,
# wrapper 들이 PATH 앞에 있으므로 변수 자체를 'nask' 같은 이름으로 덮어쓰면 됨.
MAKE_OVERRIDES=(
  MAKE="make -r"
  NASK=nask
  CC1="cc1 -I../z_tools/haribote/ -Os -Wall -quiet"
  GAS2NASK="gas2nask -a"
  OBJ2BIM=obj2bim
  BIM2HRB=bim2hrb
  BIM2BIN=bim2bin
  MAKEFONT=makefont
  BIN2OBJ=bin2obj
  EDIMG=edimg
  GOLIB=golib00
  CPP0="cpp0 -P -I../z_tools/haribote/"
  ASKA=aska
  NASKCNV=naskcnv0
  SJISCONV="sjisconv -s"
  SARTOL=sartol
  COPY=copy
  DEL=del
)

case "$TARGET" in
  clean)
    cd "$ROOT/harib27f" && make "${MAKE_OVERRIDES[@]}" clean || true
    cd "$ROOT/harib27f/haribote" && make "${MAKE_OVERRIDES[@]}" clean || true
    ;;
  haribote/|haribote)
    cd "$ROOT/harib27f/haribote" && make "${MAKE_OVERRIDES[@]}"
    ;;
  app)
    APP="${2:-}"
    [[ -z "$APP" ]] && { echo "사용법: $0 app <앱디렉터리>"; exit 2; }
    cd "$ROOT/harib27f/$APP" && make "${MAKE_OVERRIDES[@]}"
    ;;
  harib27f|"")
    cd "$ROOT/harib27f" && make "${MAKE_OVERRIDES[@]}"
    ;;
  *)
    cd "$ROOT/$TARGET" && make "${MAKE_OVERRIDES[@]}"
    ;;
esac

echo "[build] done."
