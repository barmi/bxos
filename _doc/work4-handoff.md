# work4 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work4.md](work4.md) 작업을 새 세션에서 이어받기 위한 단일 진입점이다. 먼저 work4.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 잡으면 된다.

work1 (쓰기 가능 FAT16 + OS/앱 분리) → work2 (서브디렉터리 / cwd / path) → work3 (한글 EUC-KR/UTF-8 출력) 다음 작업이다.

---

## 1. 한 줄 요약

BxOS에 **마우스와 키보드 모두로 조작 가능한 2-pane 파일 탐색기 앱 `explorer.he2`** 를 추가한다. 왼쪽에는 디렉터리 트리, 오른쪽에는 파일 목록을 표시하고, 상단에는 메뉴/아이콘 바를 배치한다. 창 크기 변경 시 tree/list/status 영역이 비율에 맞게 재배치되어야 한다.

## 2. 현재 위치 (2026-04-30 기준)

- work4 는 **Phase 0~4 구현 완료, QEMU smoke 대기 → Phase 5 (파일 열기 / 앱 실행 / preview) 진입 직전** 단계다.
- [_doc/work4.md](work4.md) 가 정본 계획 문서다. Phase 0 잠금 결정 표는 §3 Phase 0 마지막, Phase 1/2/3 구현 노트는 각 phase 끝에 있다.
- 이 handoff 문서는 다음 세션이 바로 Phase 5 에 들어갈 수 있도록 핵심 결정을 압축한 것이다.
- Phase 0 에서 잠근 핵심:
  - syscall 번호 32~39 (파일관리), 40~43 (윈도우 이벤트/리사이즈) 확정.
  - 사용자 구조체 `BX_DIRINFO`, `BX_EVENT` 필드 확정 (필드 추가는 가능, 의미 보존).
  - explorer MVP 범위, 기본/최소 창 크기, tree 폭 정책, Enter/double-click 분기, `api_exec` cwd 상속 정책 확정.
  - work2 §161 의 “`api_mkdir`/`api_rmdir` 미도입” 결정을 의도적으로 뒤집음(콘솔 built-in 은 유지하면서 사용자 syscall 만 추가).
  - app window resize opt-in 정책(`api_set_winevent` flags + 앱 buffer 교체 절차) 을 work4.md Phase 2 머리에 메모로 기록.
  - Phase 4까지의 후속 결정: double-click 판정은 현재 앱 측 같은-row 재click, `api_capturemouse` 는 미도입, `api_rename` 은 같은 부모 file/dir rename 지원(cross-parent move 는 -2).
- 현재 코드 상태에서 가능한 것:
  - 콘솔에서 `dir`, `cd`, `mkdir`, `rmdir`, `cp`, `mv`, `rm`, `type` 가능.
  - HE2 앱에서 파일 read/write/delete 가능.
  - HE2 앱에서 cwd 조회 가능 (`api_getcwd`).
  - HE2 앱에서 창 열기/그리기/키 입력 받기 가능.
  - HE2 앱에서 디렉터리 열람, stat, mkdir/rmdir/rename, 앱 실행 syscall 호출 가능.
  - HE2 window 앱에서 mouse/resize event 수신 가능.
  - `explorer.he2` 가 2-pane tree/list UI, 키보드/마우스 선택, resize layout, splitter drag 를 제공.
- 현재 코드 상태에서 부족한 것:
  - explorer 에서 `.HE2` 실행 동작 연결 전.
  - explorer preview mode 전.
  - explorer 내부 mkdir/delete/rename 입력 UI 전.
  - 긴 목록 paging/scroll polish 전.

## 3. 확정할 핵심 결정

work4.md §2 가 정본이다. 요약:

- 앱 이름은 `explorer.he2`, 콘솔 실행명은 `explorer`.
- HE2 window subsystem 앱으로 만든다.
- UI는 2-pane 구조:
  - 상단 toolbar/menu icon bar.
  - 왼쪽 directory tree.
  - 가운데 splitter.
  - 오른쪽 file list.
  - 하단 status bar.
- toolbar 는 work4 에서 배치와 일부 기본 기능 연결까지만 한다. 전체 기능 완성은 후속.
- 키보드와 마우스 모두 지원한다.
- 마우스:
  - tree/file list single click selection.
  - double-click open.
  - splitter drag 로 tree/list 비율 조정.
  - toolbar hover/pressed/disabled 표현.
- 창 resize:
  - toolbar/status 는 고정 높이.
  - tree/list 는 남은 영역에서 비율 기반 재배치.
  - tree width 는 30~35% 기본, 최소 96px.
  - 최소 창 크기는 약 320×200px.
- Enter/double-click:
  - 디렉터리면 진입.
  - `.HE2` 파일이면 실행.
  - 그 외 파일이면 preview.
- Backspace 또는 ← 는 parent/collapse.
- Tab 은 tree/list focus 전환.
- 사용자 파일관리 syscall 32~39 예약.
- 사용자 window event/resize syscall 40~43 예약.

## 4. 제안 syscall / ABI

Phase 1/2 에서 최종 확정해야 한다. 현재 계획안:

### 파일관리 API

| edx | 이름 | 의미 |
|---|---|---|
| 32 | `api_opendir(path)` | 디렉터리 handle 열기. 실패 시 0. |
| 33 | `api_readdir(handle, out)` | 다음 entry 읽기. 1=entry, 0=end, -1=error. |
| 34 | `api_closedir(handle)` | 디렉터리 handle 닫기. |
| 35 | `api_stat(path, out)` | 파일/디렉터리 정보 조회. |
| 36 | `api_mkdir(path)` | 디렉터리 생성. |
| 37 | `api_rmdir(path)` | 빈 디렉터리 삭제. |
| 38 | `api_rename(oldpath, newpath)` | 파일 rename/move. 디렉터리 지원 여부는 구현 중 결정. |
| 39 | `api_exec(path, flags)` | 앱 실행. 첫 구현은 flags=0 만. |

사용자 노출 구조체 초안:

```c
struct BX_DIRINFO {
    char name[13];              /* "NAME.EXT" + NUL, directory는 name only */
    unsigned char attr;         /* FAT attr, 0x10 = directory */
    unsigned int size;
    unsigned int clustno;
};
```

### 윈도우 이벤트 / 리사이즈 API

| edx | 이름 | 의미 |
|---|---|---|
| 40 | `api_getevent(ev, mode)` | key/mouse/resize event 수신. mode=0 poll, mode=1 wait. |
| 41 | `api_resizewin(win, buf, w, h, col_inv)` | 앱이 새 buffer 를 제공하고 window buffer 를 교체. |
| 42 | `api_set_winevent(win, flags)` | mouse/resize/double-click event 수신 여부 설정. |
| 43 | `api_capturemouse(win, enable)` | 예약만 유지. Phase 4 splitter drag 는 앱 측 상태로 처리해 아직 미도입. |

event 구조체 초안:

```c
struct BX_EVENT {
    int type;       /* KEY, MOUSE_DOWN, MOUSE_UP, MOUSE_MOVE, MOUSE_DBLCLK, RESIZE */
    int win;
    int x, y;       /* client 좌표 */
    int button;     /* bitmask */
    int key;        /* key event 일 때 */
    int w, h;       /* resize event 일 때 */
};
```

주의:
- 커널 내부 `struct FILEINFO` 를 그대로 노출하지 않는다.
- task 별 dir handle 슬롯을 두고, task 종료 시 자동 close 해야 한다.
- app mouse event 는 title bar/border/close button 시스템 동작과 충돌하면 안 된다.
- app resize 는 커널이 사용자 buffer 를 임의 확장하지 않고, 앱이 새 buffer 를 제공하는 방식으로 설계한다.
- `api_exec` 는 탐색기 현재 cwd 를 실행 앱에 상속해야 한다.

## 5. Phase 한눈에 보기

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 요구사항 / 인터페이스 확정 ☑ | 0.5d | 2-pane UI, mouse/resize, syscall 번호 확정 (2026-04-29 완료) |
| 1. 커널 디렉터리 API / 파일관리 syscall ☑ | 2d | `api_opendir`~`api_exec` (edx 32~39), libbxos wrapper, lsdir 검증앱 (2026-04-30 코드 완료, QEMU smoke 대기) |
| 2. 앱 윈도우 mouse / resize event API ☑ | 2d | `api_getevent`/`api_resizewin`/`api_set_winevent` (edx 40~42), `BX_EVENT` 큐 + fifo wakeup marker, evtest 검증앱 (2026-04-30 코드 완료, QEMU smoke 대기) |
| 3. 2-pane explorer 읽기 전용 MVP ☑ | 2d | `EXPLORER.HE2`, toolbar/tree/list/status, keyboard+mouse selection, lazy expand, resize 대응 (2026-04-30 코드 완료, QEMU smoke 대기) |
| 4. 리사이즈 대응 레이아웃 polish ☑ | 1.5d | `layout_compute` rect 산출, splitter drag, tree ratio 보존, 긴 path/text 잘라쓰기 (2026-04-30 코드 완료, QEMU smoke 대기) |
| 5. 파일 열기 / 앱 실행 / preview | 1.5d | `.HE2` 실행, text/raw preview |
| 6. 파일관리 동작 | 2d | mkdir, delete, rename/move |
| 7. UI polish / 오류 처리 | 1d | 긴 목록, 긴 path, mouse/keyboard focus 안정화 |
| 8. 문서 / 회귀 검증 | 1d | BXOS-COMMANDS/README/SETUP 갱신, clean build, QEMU smoke |

총 12~14 작업일 예상.

## 6. 코드 길잡이

| 영역 | 볼 파일 |
|---|---|
| syscall dispatcher | [harib27f/haribote/console.c](../harib27f/haribote/console.c) `hrb_api` |
| mouse/window event routing | [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) mouse loop, [harib27f/haribote/sheet.c](../harib27f/haribote/sheet.c) |
| window drawing / title / resize flags | [harib27f/haribote/window.c](../harib27f/haribote/window.c), [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) |
| FS path / directory iterator | [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c), [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) |
| task/file handles | [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) `struct TASK`, `struct FILEHANDLE` |
| user wrappers | [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h), [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) |
| HE2 app build helper | [he2/cmake/HE2App.cmake](../he2/cmake/HE2App.cmake) |
| app list / data.img wiring | [CMakeLists.txt](../CMakeLists.txt) `BXOS_HE2_APPS_WINDOW`, `BXOS_DATA_IMG_FILES` |
| window app examples | [harib27f/winhelo3/winhelo3.c](../harib27f/winhelo3/winhelo3.c), [harib27f/tetris/tetris.c](../harib27f/tetris/tetris.c) |
| command docs | [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) |

## 7. 빠른 빌드/실행 치트시트

```bash
cmake -S . -B build/cmake
cmake --build build/cmake
./run-qemu.sh
```

탐색기 구현 후 예상 QEMU 명령:

```text
> explorer
> explorer /sub
> start explorer
```

호스트 검증:

```bash
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/
fsck_msdos -n build/cmake/data.img
```

## 8. 바로 시작할 때 할 일

Phase 0~4 는 코드 완료되었으므로 다음 세션은 QEMU smoke 후 Phase 5 (`.HE2` 실행 + preview) 로 진입한다.

1. `git status --short` 로 현재 작업트리 확인.
2. **Phase 1~4 QEMU smoke**: `./run-qemu.sh` 후 콘솔에서
   - Phase 1: `lsdir /` 가 root entries 끝까지 출력. `lsdir /sub` (mkdir /sub 후 진입) 정상. 회귀: `dir /`, `mkdir/rmdir`, `cd`, `pwd`, `type hangul.utf` 변화 없음.
   - Phase 2: `start evtest` 후 client click → 점/박스 표시. drag → 점선. 우하단 모서리 drag-release → 새 크기로 frame 재배치. 회귀: tetris 키보드 입력, title bar drag, 콘솔 resize 동작 변화 없음.
   - Phase 3: `explorer` 실행 시 왼쪽 tree(`/` + 자식들), 오른쪽 file list. ↑↓/Tab/Enter/`r`/`q` 키보드 동작. row click 선택, 같은 row 재click → open(tree expand 또는 list dir 진입). `mkdir /sub` → `explorer /sub` 시 chain expand.
   - Phase 4: splitter drag 로 tree/list 폭 변경. 창을 크게/작게 resize 한 뒤에도 변경한 비율이 유지되는지 확인. 최소 크기에서 toolbar/path/status/list text 가 겹치지 않는지 확인. resize 20회 반복.
3. **Phase 5 진입 — 파일 열기 / 앱 실행 / preview**:
   - work4.md §3 Phase 5 참조.
   - `on_key()` 의 list Enter/Right 경로와 `on_mouse_down()` 의 list 같은-row 재click 경로를 공통 `open_selected_file()` 류 helper 로 묶는다.
   - 디렉터리는 기존 `enter_dir_path()` 유지, `.HE2` 파일은 `api_exec(selected_path, 0)` 로 실행, 그 외 파일은 preview mode 로 전환한다.
   - preview mode 는 우선 같은 창의 list 영역을 재사용해 text/hex lines 를 그리면 된다. ESC/Backspace 로 목록으로 복귀.
   - 실행 실패와 preview read 실패는 status bar 에 표시하고 list/tree state 를 유지한다.
4. Phase 5 끝나면 Phase 6 (mkdir/rmdir/rename/delete), Phase 7 (긴 목록/오류 처리), Phase 8 (문서/회귀 검증) 순서로 진행.

## 9. 함정으로 미리 알아둘 것

- `DIR_ITER` 는 커널 내부 iterator 이므로 사용자 포인터에 직접 넘기면 안 된다.
- dir handle 은 task 종료 때 닫혀야 한다. 앱이 X 버튼으로 강제 종료되어도 누수되면 안 된다.
- `api_readdir` 는 deleted entry, volume label, LFN slot 을 사용자 앱에 넘기지 않는 편이 낫다.
- `api_exec` 는 기존 `cmd_app` 경로를 재사용하되, 콘솔 출력/에러 경로와 cwd 상속을 조심해야 한다.
- app mouse event 는 title bar drag, close button, window resize edge 와 분리해야 한다.
- app resize 는 사용자 buffer 교체 설계가 핵심이다. 커널이 기존 앱 buffer 크기를 모른 채 크게 그리면 메모리 오염 위험이 있다.
- splitter drag 는 `api_capturemouse` 없이 구현되어 client 밖 release 를 놓칠 수 있다. 앱은 다음 click/resize 에서 drag 상태를 정리한다.
- 파일명은 8.3 이다. 앱 입력창에서 긴 이름을 silent truncation 하지 말고 경고하거나 거부한다.
- 바이너리 preview 는 제어문자를 그대로 출력하지 말고 hex fallback 을 사용한다.
- FAT write 후에는 항상 `fsck_msdos -n` 로 호스트 검증한다.

## 10. 작업하지 말아야 할 것

- LFN/한글 파일명.
- recursive delete/copy.
- 다중 선택.
- toolbar 전체 기능 완성.
- 이미지 썸네일/아이콘 리소스 시스템.
- 파일 편집기.
- 여러 드라이브 지원.
