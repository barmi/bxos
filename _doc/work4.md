# work4 — 파일 탐색기 애플리케이션 계획

## 1. 배경 / 목표

### 현재 상태 (work3 종료 시점)
- 데이터 디스크는 FAT16 32MB `data.img` 로 분리되어 있고, BxOS 내부에서 파일/디렉터리 생성, 삭제, 복사, 이동, cwd 기반 경로 해석이 가능하다.
- 콘솔 명령은 `dir`, `cd`, `pwd`, `mkdir`, `rmdir`, `cp`, `mv`, `rm`, `touch`, `echo > file`, `mkfile`, `type` 를 제공한다.
- 사용자 앱 API는 파일 read/write/delete 와 `api_getcwd` 까지 제공한다.
  - 읽기: `api_fopen`, `api_fread`, `api_fseek`, `api_fsize`, `api_fclose`
  - 쓰기/삭제: `api_fopen_w`, `api_fwrite`, `api_fdelete`
  - cwd: `api_getcwd`
- 하지만 사용자 앱이 디렉터리를 직접 열람할 API가 없다. `explorer.he2` 같은 앱은 현재 상태만으로는 파일 목록을 그릴 수 없다.
- 사용자 앱이 다른 앱을 실행하는 API도 없다. 탐색기에서 `.he2` 를 선택해 실행하려면 커널 쪽 실행 syscall 이 필요하다.
- 앱 윈도우에는 키보드 입력은 전달되지만, 앱별 마우스 클릭/더블클릭/드래그 이벤트 API는 아직 없다. 마우스는 현재 창 포커스/이동/닫기 같은 시스템 동작에만 쓰인다.
- 앱 윈도우는 `api_openwin()` 에서 앱 버퍼 크기를 고정으로 연결한다. 현재 커널은 앱 윈도우의 resize flag 를 꺼 두므로, 앱이 창 크기 변경을 받아 레이아웃을 다시 그리는 경로가 없다.

### 이번 작업의 목표
1. **파일 탐색기 앱 `explorer.he2`** 를 추가한다.
   - 왼쪽: 디렉터리 트리.
   - 오른쪽: 선택 디렉터리의 파일 목록.
   - 상단: 메뉴/아이콘 바. 이번 work4 에서는 배치와 표시만 하고 실제 기능 연결은 최소화한다.
   - 하단: status bar.
2. **키보드와 마우스 모두로 동작**하게 한다.
   - 키보드: 이동, 열기, 상위 이동, 새로고침, 파일관리 단축키.
   - 마우스: 트리/파일 목록 선택, 더블클릭 열기, splitter 드래그, toolbar 버튼 hover/press 표현.
3. **창 크기 조절에 반응하는 레이아웃**을 만든다.
   - 창이 커지면 트리/목록 영역이 비율에 맞게 확장된다.
   - 창이 작아지면 최소 폭/높이를 지키고, 텍스트를 줄여 겹침을 방지한다.
4. 탐색기에 필요한 **사용자 공간 파일관리 API** 를 추가한다.
   - 디렉터리 열기/읽기/닫기.
   - stat, mkdir, rmdir, rename/move.
   - 앱 실행.
5. 탐색기에 필요한 **앱 윈도우 이벤트/리사이즈 API** 를 추가한다.
   - 앱별 mouse down/up/move/double-click 이벤트.
   - 앱 윈도우 resize 허용.
   - resize event 전달 및 앱 버퍼 교체/재연결.
6. 기존 콘솔/FS 동작을 회귀시키지 않고, 콘솔 명령과 탐색기가 같은 FAT16 경로 규칙을 공유한다.

## 2. 설계 결정 사항 (계획안 — 2026-04-29)

| 항목 | 결정 | 비고 |
|---|---|---|
| 앱 이름 | `explorer.he2` | 콘솔에서는 `explorer` 로 실행. 인자 없으면 현재 cwd, 인자 있으면 해당 경로에서 시작. |
| 앱 종류 | HE2 window subsystem | `he2_add_app(explorer ... SUBSYSTEM WINDOW)` 로 빌드. |
| UI 구조 | 2-pane explorer | 상단 toolbar, 왼쪽 tree, 오른쪽 file list, 하단 status. |
| 상단 메뉴/아이콘 바 | 배치만 선구현 | Back/Up/Refresh/New/Delete/Rename 같은 슬롯을 둔다. 기능 연결은 work4 에서 가능한 것만. |
| 기본 창 크기 | 약 420×280 px | 640×480 화면에서 기본 콘솔/디버그 창과 같이 써도 부담 없는 크기. |
| 최소 창 크기 | 약 320×200 px | 이보다 작아지면 resize clamp. toolbar/status/list text 가 겹치지 않아야 함. |
| 리사이즈 모델 | 앱 윈도우 resizable + resize event | 커널이 app window resize 를 허용하고, 앱이 새 크기에 맞는 buffer 를 할당해 다시 연결한다. |
| 레이아웃 규칙 | toolbar/status 고정 높이, 중앙 영역 가변 | 왼쪽 tree 는 전체 폭의 30~35%, 최소 96px. 오른쪽 list 는 나머지. splitter 드래그로 비율 조정. |
| 입력 모델 | 키보드 + 마우스 | 키보드만으로 모든 기능 가능해야 하고, 마우스는 선택/열기/드래그/toolbar 조작을 제공. |
| 마우스 동작 | single click select, double click open | tree 노드 클릭 시 해당 디렉터리 선택, file list 더블클릭 시 열기. |
| 키보드 동작 | ↑/↓/←/→, Enter, Backspace, Tab, `r`, `n`, `m`, `d`, `q`, `?` | Tab 으로 tree/list focus 전환. |
| 파일 목록 컬럼 | 이름, 종류, 크기 | FAT 8.3 이름을 `NAME.EXT` 형태로 표시. 디렉터리는 `<DIR>`. |
| 트리 표시 | lazy depth tree | root 부터 시작하고, 펼친 디렉터리만 하위 목록을 읽는다. 깊이 표시용 indent. |
| 정렬 | 디렉터리 먼저, 그다음 파일, 이름 오름차순 | 사용자 공간에서 정렬. |
| 경로 모델 | 기존 work2 규칙 유지 | `/` 절대, 상대 경로, `.`/`..`, 8.3 이름. 드라이브 prefix 없음. |
| Enter/double-click 동작 | 디렉터리면 진입, `.HE2` 면 실행, 그 외 파일이면 preview | 실행은 신규 `api_exec` 사용. |
| 파일 preview | 1차는 text/raw viewer 내장 | 최대 N KB 를 읽어 동일 창 preview mode 에 표시. 바이너리는 hex/size 안내. |
| 파일 생성/삭제 | 1차는 `mkdir`, `rmdir`, file delete, rename/move | 파일 복사와 recursive 작업은 후속. |
| 디렉터리 API | handle 기반 `api_opendir`, `api_readdir`, `api_closedir` | task 별 dir handle 배열을 둔다. |
| 사용자 dir entry 구조 | `struct BX_DIRINFO` | `name[13]`, `attr`, `size`, `cluster` 정도만 앱에 노출. FAT 내부 구조체 직접 노출 금지. |
| 파일관리 syscall 번호 | 32~39 예약 | 31=`api_getcwd` 다음부터 연속 배치. |
| 윈도우 이벤트 syscall 번호 | 40~43 예약 | event poll/wait, resize buffer 교체, mouse capture 등. 세부는 Phase 2 에서 확정. |
| 언어/인코딩 | 파일명 UI는 ASCII 8.3 기준 | 한글 파일명/LFN은 범위 밖. 본문 preview 는 현재 `langmode` 에 맡김. |

## 3. 작업 단계

체크박스(☐)는 PR 또는 커밋 단위로 끊을 수 있는 자연스러운 경계를 표시한다.

### Phase 0 — 요구사항 / 인터페이스 확정 (0.5일) — ☑ 완료 (2026-04-29)
- ☑ 본 문서와 [work4-handoff.md](work4-handoff.md) 를 정본으로 두고, MVP 범위를 잠근다.
- ☑ 탐색기 MVP 기능을 다음으로 확정한다:
  - ☑ 2-pane UI: 왼쪽 tree, 오른쪽 file list.
  - ☑ 상단 toolbar 배치.
  - ☑ 키보드 + 마우스 선택/열기.
  - ☑ 창 resize 에 따른 레이아웃 재배치.
  - ☑ 파일 preview.
  - ☑ `.he2` 실행.
  - ☑ mkdir, rmdir, rename/move, file delete.
- ☑ 신규 파일관리 syscall 번호와 사용자 구조체 이름을 확정한다. → **edx 32~39 + `struct BX_DIRINFO`** (§2 표 / §4 handoff 와 동일).
- ☑ 신규 윈도우 이벤트/리사이즈 syscall 번호와 event 구조체를 확정한다. → **edx 40~43 + `struct BX_EVENT`** (§2 표 / §4 handoff 와 동일). edx=43 은 `api_setcursor(shape)` 로 확정.
- ☑ recursive copy/delete, LFN, 다중 선택, toolbar 전체 기능 구현은 범위 밖으로 명시한다. (§6)

**확인할 사항**
- ☑ work4.md / work4-handoff.md 에 같은 syscall 번호, 같은 UI 구조, 같은 MVP 범위가 적혀 있다.
- ☑ 기존 work1~3 문서와 충돌하는 결정이 없다.
  - 단, [work2.md](work2.md) §161 의 “`api_mkdir`/`api_rmdir` 는 도입하지 않는다 — 콘솔 built-in 으로 충분” 결정을 **work4 에서 의도적으로 뒤집는다**. 이유: explorer 가 사용자 앱에서 디렉터리 생성/삭제를 트리거해야 하므로 콘솔 명령만으로는 부족. 신규 syscall 추가는 work2 결정의 확장이지 폐기가 아니다 — 콘솔 built-in 은 그대로 유지된다.
  - work2 path resolution 규칙(상대/절대, `.`/`..`, MAX_PATH=128B) 과 work3 EUC-KR/UTF-8 출력 규칙은 그대로 유지한다. explorer 는 path 인자에 work2 규칙을 그대로 따르고, preview 는 현재 `langmode` 를 변경하지 않는다.
- ☑ 현재 커널의 app window resize 제한을 어떻게 풀지 Phase 2 의 설계 메모에 적었다. → 아래 Phase 2 §“Resize 정책 설계 메모” 참조.

**Phase 0 잠금 결정 요약**

아래는 Phase 1 이후 변경 시 work4.md / work4-handoff.md 두 문서 모두 갱신해야 하는 항목이다.

| 잠금 항목 | 값 |
|---|---|
| 사용자 파일관리 syscall | edx 32~39 (`opendir`/`readdir`/`closedir`/`stat`/`mkdir`/`rmdir`/`rename`/`exec`) |
| 사용자 dir entry 구조체 | `struct BX_DIRINFO { char name[13]; unsigned char attr; unsigned int size; unsigned int clustno; }` |
| 사용자 윈도우 이벤트 syscall | edx 40~43 (`getevent`/`resizewin`/`set_winevent`/`capturemouse`) |
| 사용자 event 구조체 | `struct BX_EVENT { int type, win, x, y, button, key, w, h; }` (필드 추가는 가능, 기존 필드 의미는 보존) |
| event type 상수 | `BX_EVENT_KEY`, `BX_EVENT_MOUSE_DOWN`, `BX_EVENT_MOUSE_UP`, `BX_EVENT_MOUSE_MOVE`, `BX_EVENT_MOUSE_DBLCLK`, `BX_EVENT_RESIZE` |
| explorer 콘솔명/파일명 | `explorer` / `EXPLORER.HE2` |
| 기본/최소 창 크기 | 기본 420×280, 최소 320×200 |
| tree 폭 정책 | 30~35% 비율, 최소 96px, splitter drag 가능 |
| Enter/double-click 분기 | dir → 진입, `.HE2` → `api_exec`, 그 외 → preview |
| `api_exec` cwd 정책 | 탐색기 현재 경로 상속 |
| Phase 2 이후 결정 | (a) double-click 판정은 현재 앱 측 같은-row 재click, (b) `api_capturemouse` 대신 edx=43 `api_setcursor(shape)` 도입, (c) `api_rename` 은 같은 부모 file/dir rename 지원, cross-parent move 는 -2 |

### Phase 1 — 커널 디렉터리 API / 사용자 파일관리 syscall (2일) — ☑ 구현 완료 (2026-04-30)
**목표**: `explorer.he2` 가 디렉터리를 읽고 파일관리 명령을 요청할 수 있는 최소 ABI를 만든다.

- ☑ [bootpack.h](../harib27f/haribote/bootpack.h) 에 사용자 노출용 구조체 추가:
  - ☑ `struct BX_DIRINFO { char name[13]; unsigned char attr; unsigned int size; unsigned int clustno; };`
  - ☑ `struct DIRHANDLE { int in_use; struct DIR_ITER it; }` + `DIR_HANDLES_PER_TASK=4` + `task->dhandle` slot pointer.
- ☑ [fs_fat.c](../harib27f/haribote/fs_fat.c) 의 `DIR_ITER` 를 사용자 API가 안전하게 사용할 수 있는 래퍼로 감싼다 → `fs_user_opendir/readdir/stat/rename` + `fs_dirinfo_from_finfo`. deleted/0xE5, LFN 0x0F, volume label 0x08 은 readdir 에서 자동 skip.
- ☑ [console.c](../harib27f/haribote/console.c) `hrb_api` 에 파일관리 syscall 추가:
  - ☑ `edx=32 api_opendir(path)` → DIRHANDLE* 또는 0.
  - ☑ `edx=33 api_readdir(handle, BX_DIRINFO *out)` → 1=entry, 0=end, -1=error.
  - ☑ `edx=34 api_closedir(handle)`.
  - ☑ `edx=35 api_stat(path, BX_DIRINFO *out)` (root "/" 는 dir attr 만 채워 0 반환).
  - ☑ `edx=36 api_mkdir(path)` → fs_mkdir 위임.
  - ☑ `edx=37 api_rmdir(path)` → fs_rmdir 위임.
  - ☑ `edx=38 api_rename(oldpath, newpath)` → 같은 부모 in-place rename(file/dir 둘 다); cross-parent move 는 -2 반환 (Phase 6 에서 다시 검토).
  - ☑ `edx=39 api_exec(path, flags)` → 실행 파일을 resolve 하고 새 console task 를 spawn. 실행 파일의 부모 디렉터리를 cwd 로 설정하고 fifo 에 basename+Enter 를 주입.
- ☑ task 종료 시 열려 있는 dir handle 을 정리한다 — `load_and_run_he2` 와 legacy `cmd_app` 양쪽 cleanup 에 dir handle close 추가.
- ☑ [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h) 와 [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) 에 wrapper 추가 (`api_opendir`~`api_exec`).
- ☑ legacy [harib27f/apilib.h](../harib27f/apilib.h) 에 선언 맞춤. **NASM wrapper 는 도입하지 않음** — explorer/lsdir 등 dir/exec 를 호출하는 앱은 모두 HE2 빌드라 libbxos C wrapper 만으로 충분. 향후 legacy `harib27f/apilib/api032.nas..api039.nas` 가 필요해지면 그때 추가.

**검증/확인**
- ☑ `cmake --build build/cmake --target kernel` 통과.
- ☑ `cmake --build build/cmake` clean build 통과.
- ☑ `fsck_msdos -n build/cmake/haribote.img` / `data.img` clean.
- ☑ 검증 앱 [harib27f/lsdir/lsdir.c](../harib27f/lsdir/lsdir.c) 추가 — `lsdir [path]` 가 `api_opendir/readdir/closedir` end-to-end 를 검증하고 entry 수와 size/`<DIR>` 표시까지 출력한다. CMake `BXOS_HE2_APPS_BASIC` 에 등록되어 `data.img` 에 자동 포함.
- ☐ QEMU smoke (사용자 인터랙션 필요): `./run-qemu.sh` → `lsdir /`, `lsdir /sub`, `mkdir /tmp4`, `rmdir /tmp4`, 콘솔 회귀(`dir /`, `cd /sub`, `pwd`, `type hangul.utf`).
- ☑ 잘못된 path / 파일에 대한 opendir / 잘못된 handle: 코드 경로상 모두 -1 또는 0 반환 (DIRHANDLE pointer 범위 검사로 panic 방지).
- ☑ `api_closedir()` 후 handle 재사용 가능 — `in_use=0` 으로 reset 후 opendir 가 다시 잡음.

**Phase 1 구현 노트**
- `g_memtotal` 글로벌을 [bootpack.h](../harib27f/haribote/bootpack.h)/[bootpack.c](../harib27f/haribote/bootpack.c) 에 추가해 `api_exec` 가 `open_constask(0, g_memtotal)` 을 호출할 수 있게 한다. HariMain 의 `memtest` 직후 캐시.
- DIRHANDLE 슬롯은 console_task 의 stack 에 놓고 `task->dhandle` 에 연결한다 (FILEHANDLE 와 동일한 패턴, MAX=4).
- `api_rename` 의 cross-parent move 와 디렉터리 move 는 Phase 6 에서 cmd_mv 의 copy+unlink 경로를 재사용해 검토.

### Phase 2 — 앱 윈도우 mouse / resize event API (2일) — ☑ 구현 완료 (2026-04-30)
**목표**: 탐색기 앱이 마우스와 창 크기 변경을 직접 처리할 수 있게 한다.

**Resize 정책 설계 메모 (Phase 0 에서 확정)**

현재 [console.c](../harib27f/haribote/console.c) `hrb_api` 의 `edx == 5` (`api_openwin`) 에서

```c
sht->flags |= 0x10;                       /* app-created window */
sht->flags &= ~SHEET_FLAG_RESIZABLE;      /* 앱 buffer 고정크기 → 리사이즈 금지 */
```

로 모든 앱 창의 resizable flag 를 무조건 끈다. 앱 buffer 가 사용자 메모리이므로 커널이 임의로 늘릴 수 없기 때문이다. work4 에서는 이 제한을 다음 절차로 푼다.

1. **opt-in API**: 신규 `api_set_winevent(win, flags)` (edx=42) 의 flags bitmask 에 `BX_WIN_RESIZABLE` 비트를 둔다. 앱이 명시적으로 요청한 창만 `SHEET_FLAG_RESIZABLE` 가 켜진다. 기본은 기존과 동일(불가). → tetris/winhelo3 등 기존 앱은 영향 없음.
2. **resize 트리거**: 사용자가 resize edge 를 드래그하면 커널은 새 client w/h 를 계산해 `BX_EVENT_RESIZE { w, h }` 만 task FIFO 로 전달한다. 이 시점에서는 sheet buffer 를 교체하지 않고 sheet 의 시각적 영역도 늘리지 않는다(드래그 outline 만 표시).
3. **buffer 교체**: 앱이 새 크기에 맞는 buffer 를 `api_malloc` 으로 확보한 뒤 `api_resizewin(win, new_buf, new_w, new_h, col_inv)` (edx=41) 을 호출한다. 커널은 sheet 의 `buf`/`bxsize`/`bysize` 를 교체하고 새 영역을 inv-color 로 초기화한 뒤 sheet 를 refresh 한다. 이전 buffer 는 앱이 `api_free` 한다(이중 해제 방지: 커널은 buffer 소유권을 가지지 않는다).
4. **시스템 동작 보존**: title bar drag, close button, system 측 resize edge 처리(outline drag) 는 그대로 시스템이 담당한다. 앱은 client area 의 mouse event 와 resize 결과만 받는다.
5. **실패 처리**: `api_resizewin` 호출 전에 다른 resize event 가 들어오면 마지막 값으로 합쳐진다(coalesce). 앱이 응답하지 않아도 sheet 는 옛 크기로 그대로 보인다.

이 메모의 세부 인자/필드는 Phase 2 구현 중 확정했다. 이후 변경 결정은 위 Phase 0 표의 “Phase 2 이후 결정” 항목을 참고.

- ☑ 앱용 event 구조체를 정의한다 → [bootpack.h](../harib27f/haribote/bootpack.h) `struct BX_EVENT { int type, win, x, y, button, key, w, h }` + `BX_EVENT_KEY/MOUSE_DOWN/UP/MOVE/DBLCLK/RESIZE` 상수. 사용자에 동일 정의를 [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h) / [harib27f/apilib.h](../harib27f/apilib.h) 에 노출.
- ☑ [bootpack.c](../harib27f/haribote/bootpack.c) 의 마우스 처리에서 app window 내부 클릭/이동 이벤트를 해당 task event 큐로 전달:
  - 마우스 루프 끝에서 topmost APP_EVENTS sheet 를 다시 hit-test. client area = `(3 ≤ x < bxsize-3 && 21 ≤ y < bysize-3)` 에서 close button / resize 핸들 영역 제외.
  - btn edge change → MOUSE_DOWN/UP. 같은 btn 상태 + (mdec.x|y != 0) → MOUSE_MOVE.
  - drag 중인 scrollbar/title-drag/window-move/resize-drag 모드에서는 라우팅을 건너뛴다 (시스템 행위 우선).
- ☑ title bar / close button / system resize edge 시스템 동작 보존 — 새 코드는 그 분기 다음에 별도 hit-test 한 후에만 이벤트를 push.
- ☑ app window resize opt-in:
  - `api_set_winevent(win, flags)` (edx=42) 가 `BX_WIN_EV_RESIZE` 비트로 `SHEET_FLAG_RESIZABLE` 을 켠다. 기존 `api_openwin` 의 무조건 clear 는 그대로 두고, 사용자가 명시적으로 요청한 창만 resize 가능.
  - `BX_WIN_EV_MOUSE` 는 `SHEET_FLAG_APP_EVENTS` (0x100) 신규 비트를 켠다.
  - `BX_WIN_EV_DBLCLK` 는 `SHEET_FLAG_APP_DBLCLK` (0x200) 비트를 켠다 — 현재 커널은 더블클릭 합성 안 함, 앱이 시간/좌표로 직접 판정. flag 는 향후 커널 합성 도입 시 사용.
- ☑ resize event 전달:
  - 사용자가 RESIZABLE 인 app window 의 우/하/우하단 모서리를 드래그 → 기존 resize-mode 로 진입. drag 중에는 sheet 를 라이브로 안 바꾸고(`SHEET_FLAG_HAS_CURSOR`/`SCROLLWIN` 분기에 안 걸림), 좌표만 누적.
  - drag 종료 (좌클릭 release) 시 `BX_EVENT_RESIZE { w=new_rw, h=new_rh }` 를 task event 큐로 push.
  - 앱은 새 buffer 를 `api_malloc` 으로 잡고 `api_resizewin(win, buf, w, h, col_inv)` (edx=41) 호출 → 커널은 `sheet_resize` 로 buf/bxsize/bysize 교체, refresh. 이전 buffer 는 호출자가 `api_free`.
- ☑ [he2/libbxos](../he2/libbxos/) wrapper 추가: `api_getevent(BX_EVENT *out, int mode)`, `api_resizewin(win, buf, w, h, col_inv)`, `api_set_winevent(win, flags)`, `api_setcursor(shape)`.

**event 전달 모델**
- 키보드 char 는 기존처럼 `task->fifo` 에 `char + 256` 으로 들어온다. 마우스/리사이즈는 `task->event_buf[BX_EVENT_QUEUE_LEN]` (32 슬롯 circular) 에 들어가고, fifo 에 `BX_EVENT_FIFO_MARKER (0x10000)` 를 넣어 task 를 깨운다.
- `api_getevent(out, mode)` 는 (a) event 큐 우선, (b) fifo 에서 키 char → `BX_EVENT_KEY` 변환, marker → 큐 다시 확인, 커서 timer/onoff/close → 기존 api_getkey 와 동일하게 처리, (c) mode==1 이면 fifo empty 시 `task_sleep` 후 retry.
- 큐 full 시 새 이벤트는 drop (오래된 클릭 손실 방지).
- `api_getkey` 와 `api_getevent` 를 같은 앱이 섞어 쓰면 marker 0x10000 가 raw key 로 잘못 보일 수 있어 **혼용 금지** (tetris 등 기존 앱은 그대로 api_getkey 만 사용 → 회귀 없음).

**검증/확인**
- ☑ `cmake --build build/cmake --target kernel` / clean build 통과.
- ☑ `fsck_msdos -n` 양쪽 이미지 clean.
- ☑ 검증 앱 [harib27f/evtest/evtest.c](../harib27f/evtest/evtest.c) 추가 — `api_set_winevent(win, MOUSE|RESIZE)` 후 `api_getevent` 으로 모든 이벤트를 스트림 처리. CMake `BXOS_HE2_APPS_WINDOW` 에 등록되어 `EVTEST.HE2` 로 data.img 에 자동 포함.
- ☐ QEMU smoke (사용자 인터랙션 필요): `./run-qemu.sh` → `start evtest`
  - client click 이 점/박스로 표시되는지.
  - drag 시 점선 자국이 그려지는지.
  - 우하단 모서리 드래그 후 release → 새 크기로 창이 갱신되는지 (frame redraw 포함).
  - title bar drag, close button, tetris 키보드 입력 회귀 없음.

**Phase 2 구현 노트**
- `BX_EVENT_QUEUE_LEN = 32`. event_buf 는 console_task 의 stack 에 stack-allocate (FILEHANDLE/DIRHANDLE 와 동일 패턴).
- `BX_EVENT_FIFO_MARKER = 0x10000` — task->fifo 의 기존 값 영역(0~3, 4, 256~767, 768~2280) 과 충돌하지 않는 큰 값.
- app exit 시 (load_and_run_he2 / 레거시 cmd_app) `event_count=0; event_p=0` 로 큐를 비우고 sheet_free 로 SHEET_FLAG_APP_EVENTS 도 자연 정리.
- `api_capturemouse` 는 Phase 4 splitter drag 구현 후에도 도입하지 않음. 대신 edx=43 `api_setcursor(shape)` 로 app client 내부 splitter hover 에서 커서 shape 를 바꿀 수 있게 했다. drag 중 client 밖 release 를 놓칠 수 있지만 다음 click/resize 에서 capture 상태를 정리하는 앱 측 안전장치로 충분하다고 판단.

### Phase 3 — `explorer.he2` 2-pane 읽기 전용 MVP (2일) — ☑ 구현 완료 (2026-04-30)
**목표**: 트리와 파일 목록을 볼 수 있고 키보드/마우스로 디렉터리를 이동할 수 있는 첫 번째 창 앱을 만든다.

- ☑ [harib27f/explorer/explorer.c](../harib27f/explorer/explorer.c) 신규 추가 (~600 LOC, 단일 파일).
- ☑ CMake 에 `explorer` 를 HE2 window 앱으로 등록 — `BXOS_HE2_APPS_WINDOW` list 에 `explorer` 추가. `EXPLORER.HE2` 가 `data.img` 에 자동 포함.
- ☑ 앱 시작 시 `api_cmdline()` 으로 첫 인자를 파싱. 없으면 `api_getcwd()` 결과 사용 (둘 다 실패 시 `/`).
- ☑ `api_opendir`/`api_readdir`/`api_closedir` 로 entries 수집 + 정렬 후 앱 내부 `BX_DIRINFO files[256]` 에 저장. `.`/`..` 는 list 에서 제외 (트리 expand 로직도 동일).
- ☑ tree model:
  - ☑ root node `/` 생성, 시작 시 자동 expand.
  - ☑ 펼친 디렉터리만 lazy load (`tree_expand_at` 이 호출 시점에 opendir).
  - ☑ 시작 경로까지 자동 chain expand (`tree_init` 의 component-by-component 진행).
- ☑ file list model:
  - ☑ 현재 tree selection 의 entries 를 표시 (`update_files_for_selection`).
  - ☑ 디렉터리 우선 + 이름순 (`dirinfo_cmp` + `dirinfo_sort`).
- ☑ 2-pane UI:
  - ☑ 제목 `Explorer`, 기본 420×280, client area 414×256.
  - ☑ 상단 toolbar 22px — Back/Up/Refresh/New/Del/Ren placeholder 6개 + 현재 path 라벨.
  - ☑ 왼쪽 tree (default 30%, 최소 96px).
  - ☑ splitter 4px 회색 띠 (drag 는 Phase 4 에서 완료).
  - ☑ 오른쪽 file list: header(Name/Size) + 선택 row highlight, `<DIR>`/size 표시, 디렉터리는 진한 파랑.
  - ☑ 하단 status 18px: 진입 결과 메시지 + entries 카운트, 에러 시 빨간색.
- ☑ 키보드:
  - ☑ ↑/↓ selection (tree/list 별 focus 따라).
  - ☑ ←/Backspace tree=collapse 또는 parent, list=parent 디렉터리.
  - ☑ → tree=expand, list=Enter 와 동일 진입.
  - ☑ Enter tree=expand toggle, list=디렉터리면 진입 / 그 외 항목은 status 표시 (Phase 5 에서 preview/exec).
  - ☑ Tab tree↔list focus 전환.
  - ☑ `r` 현재 디렉터리 reload.
  - ☑ `q` / ESC 종료.
- ☑ 마우스:
  - ☑ tree/list click → row 선택 + focus 이동.
  - ☑ 같은 row 두 번 click → "open" (tree=expand toggle, list=디렉터리 진입). `last_click_pane`/`last_click_idx` 매칭 방식, double-click 합성 없이도 동작.
  - ☑ splitter drag (Phase 4 에서 완료 — 비율 조정).

**확인/검증**
- ☑ `cmake --build build/cmake` clean build 통과 (tetris/winhelo3/lines 등 기존 윈도우 앱 회귀 없음, evtest 도 정상 빌드).
- ☑ `python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/` 에 `EXPLORER.HE2` 포함 확인.
- ☑ `fsck_msdos -n build/cmake/data.img` clean (40 files).
- ☐ QEMU smoke (사용자 인터랙션 필요):
  - `./run-qemu.sh` → 콘솔에서 `explorer` 실행 시 왼쪽 tree 의 `/` 노드 + root entries, 오른쪽 file list 표시.
  - `mkdir /sub`, `touch /sub/a.txt` 후 `explorer /sub` 에서 tree 가 `sub` 까지 chain expand, list 에 `A.TXT` 표시.
  - 키보드 ↑/↓/Enter/Tab/`q` 동작.
  - 마우스 click selection / 같은 row 재click open 동작.
  - 우하단 드래그 후 release → 새 크기 layout 적용 (Phase 4 에서 layout polish).

**Phase 3 구현 노트**
- 같은 row 두 번 click 으로 open: native double-click 이 없는 환경에 대한 단순화. UX 가 약간 비표준이지만 키보드 Enter 와 함께 두 가지 진입 경로를 제공한다.
- splitter drag 와 긴 path 잘라쓰기 polish 는 Phase 4 에서 완료했다. 파일 preview 와 `.he2` exec 는 Phase 5 에서 다룬다.
- explorer 의 path 길이 한도는 work2 와 동일한 128B (`MAX_PATH`).
- 트리 노드 256개, file 256개 정적 배열 — heap 부담을 1 KB 수준으로 묶는다.

### Phase 4 — 리사이즈 대응 레이아웃 (1.5일) — ☑ 구현 완료 (2026-04-30)
**목표**: 창 크기 변경 시 toolbar/tree/list/status 가 비율에 맞게 재배치되고 텍스트가 겹치지 않게 한다.

- ☑ layout 함수 분리:
  - ☑ 입력: window/client width, height, tree ratio.
  - ☑ 출력: toolbar rect, tree rect, splitter rect, list rect, status rect.
- ☑ resize event 수신 시:
  - ☑ 새 buffer 할당.
  - ☑ `api_resizewin` 로 buffer 교체.
  - ☑ layout 재계산.
  - ☑ 전체 redraw.
- ☑ tree width 는 비율 기반으로 계산하되 최소/최대 폭 clamp.
- ☑ 목록 행 수는 창 높이에 따라 재계산한다.
- ☑ path/status/list text 는 column 폭에 맞게 잘라 표시한다.
- ☑ splitter drag 로 변경한 tree ratio 가 resize 후에도 유지된다.
- ☑ 계획 외 polish: resizable window 의 right/bottom/corner resize hit zone 에서 마우스 커서를 가로/세로/대각선 resize 모양으로 변경한다.
- ☑ 계획 외 polish: explorer tree/list pane 에 세로 스크롤바를 추가하고, splitter hover 에서 좌우 resize 커서를 표시한다.
- ☑ 계획 외 polish: app window resize 이벤트를 drag 중에도 보내 explorer resize 가 즉시 반영되게 한다. 같은 창의 pending resize 이벤트는 최신 크기로 coalesce 한다.

**확인할 사항**
- ☑ `cmake --build build/cmake` clean build 통과.
- ☑ `fsck_msdos -n build/cmake/haribote.img` / `data.img` 통과.
- ☑ `python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/` 에 `EXPLORER.HE2 12034` 확인.
- ☐ QEMU 에서 explorer 창을 크게/작게 조절해도 toolbar/tree/list/status 가 겹치지 않는다.
- ☐ 최소 크기에서 list row, status message, toolbar icon slot 이 화면 밖으로 깨지지 않는다.
- ☐ 큰 크기에서 file list 표시 행 수가 늘어난다.
- ☐ splitter drag 로 tree/list 폭이 바뀌고, 이후 창 resize 에서 비율이 유지된다.
- ☐ console/explorer resize edge 에 hover 시 resize 방향에 맞는 커서가 표시된다.
- ☐ explorer splitter hover 에서 좌우 resize 커서가 표시된다.
- ☐ explorer tree/list 에 항목이 표시 영역보다 많을 때 세로 스크롤바가 표시되고 thumb drag/track click 으로 이동한다.
- ☐ explorer window resize drag 중에도 레이아웃이 즉시 갱신된다.
- ☐ resize 를 20회 이상 반복해도 앱이 멈추거나 오래된 buffer 를 참조하지 않는다.

**Phase 4 구현 노트**
- `struct Rect` / `struct Layout` 을 추가하고 `layout_compute(cw, ch, tree_ratio, &layout)` 으로 toolbar/middle/tree/splitter/list/status rect 를 한 번에 산출한다.
- tree 폭은 `tree_ratio`(permille, 기본 320) 로 저장한다. splitter drag 는 현재 폭을 clamp 한 뒤 `tree_ratio` 를 갱신하고, resize 는 저장된 ratio 를 기준으로 새 tree/list 폭을 다시 계산한다.
- `LIST_W_MIN=80`, `TREE_W_MIN=96` 을 함께 적용해 작은 창에서도 list 영역이 완전히 사라지지 않게 했다.
- `BX_EVENT_MOUSE_DOWN/MOVE/UP` 으로 splitter drag 를 처리한다. `api_capturemouse` 는 아직 도입하지 않았으므로 client 밖에서 release 를 놓칠 수 있지만, 다음 click/resize 에서 drag 상태를 정리한다.
- path/status 는 가운데 `...` 생략, tree/list entry 는 column 폭에 맞춘 끝부분 `...` 생략을 적용했다. destination buffer 크기도 함께 받아 긴 path 가 임시 버퍼를 넘지 않도록 했다.
- list 의 Size/`<DIR>` 컬럼은 폭이 충분할 때만 표시한다. 좁은 폭에서는 이름 컬럼만 표시해 텍스트 겹침을 피한다.
- `init_mouse_cursor8()` 의 커서 atlas 를 4개(arrow, diagonal resize, horizontal resize, vertical resize) 로 확장하고, 커널 mouse loop 가 실제 resize hit-test 와 같은 조건으로 커서 buffer 를 전환한다.
- explorer tree/list scrollbar 는 각 pane 안쪽 우측에 표시한다. visible rows 보다 entry 수가 많을 때만 나타나며, thumb drag 와 track click(page up/down) 을 지원한다.
- app window resize 는 drag 중 `BX_EVENT_RESIZE` 를 계속 post 하며, event queue 안의 같은 window resize 이벤트는 최신 w/h 로 coalesce 한다.

### Phase 5 — 파일 열기 / 앱 실행 / preview (1.5일) — 구현 완료 (2026-05-01, QEMU smoke 대기)
**목표**: 탐색기에서 선택한 항목을 “열 수” 있게 한다.

- ☑ Enter 또는 double-click 동작을 확장한다:
  - ☑ 디렉터리: 진입.
  - ☑ `.HE2`: `api_exec(selected_path, 0)` 로 실행.
  - ☑ 그 외 파일: preview mode 진입.
- ☑ preview mode:
  - ☑ 작은 텍스트 파일은 앱 창 안에서 표시.
  - ☑ 너무 큰 파일은 앞부분만 읽고 status 에 truncation 표시.
  - ☑ 바이너리로 보이는 파일은 size/cluster 정보와 첫 N bytes hex 를 표시.
  - ☑ ESC 또는 Backspace 로 목록 복귀.
- ☑ toolbar placeholder 중 Back/Up/Refresh 정도는 동작 연결 여부를 결정한다. Phase 5 에서는 toolbar slot 을 계속 placeholder 로 두고, 이미 있는 keyboard `r`/Backspace 동작만 유지한다.
- ☑ `.he2` 실행 실패 시 status 에 에러 표시.
- ☑ 실행 앱이 현재 탐색기 경로를 cwd 로 상속받도록 `api_exec` 를 보정한다.

**구현 노트 (2026-05-01)**
- [explorer.c](../harib27f/explorer/explorer.c) 에 `open_selected_file()` 를 추가하고 list Enter/Right 와 list double-click 이 같은 경로를 쓰도록 했다.
- preview mode 는 기존 list pane 을 재사용한다. text 는 줄 단위로 표시하고, NUL/제어문자 비율이 높은 파일은 8 bytes/line hex view 로 fallback 하며 status 에 size/cluster 를 표시한다. preview scrollbar 도 mouse drag/track click 이 동작한다.
- `api_exec(path, 0)` 는 실행 파일을 resolve 한 뒤 새 console task 의 cwd 를 실행 파일의 부모 디렉터리로 설정하고, command fifo 에는 basename 만 주입한다. 따라서 explorer 현재 목록에서 선택한 `.HE2` 는 해당 목록 경로를 cwd 로 보고 시작한다.

**확인할 사항**
- ☐ QEMU 에서 `explorer` → `TETRIS.HE2` 선택 → Enter 또는 double-click 으로 tetris 실행.
- ☐ `explorer /sub` 에서 앱 실행 시 `pwd.he2` 또는 검증 앱이 `/sub` 를 cwd 로 본다.
- ☐ `HANGUL.UTF` preview 가 현재 `langmode 4` 에서 깨지지 않고 표시된다.
- ☐ 바이너리 파일 preview 가 화면을 망가뜨리지 않는다.

### Phase 6 — 파일관리 동작 (2일)
**목표**: 콘솔 명령으로 하던 기본 파일관리를 탐색기에서도 수행한다.

- ☐ `n` 또는 toolbar New: 새 디렉터리 만들기.
  - ☐ 앱 내부 line input UI 로 8.3 이름 입력.
  - ☐ `api_mkdir(current_path/name)` 호출.
- ☐ `d` 또는 toolbar Delete: 선택 항목 삭제.
  - ☐ 파일이면 `api_fdelete`.
  - ☐ 디렉터리면 `api_rmdir` (비어 있지 않으면 실패 메시지).
  - ☐ 삭제 전 짧은 confirm prompt.
- ☐ `m` 또는 toolbar Rename: rename/move.
  - ☐ 새 이름 또는 경로 입력.
  - ☐ `api_rename`/`api_move` 호출.
- ☐ 동작 후 tree/file list reload 및 selection 보정.
- ☐ root 의 `.`/`..` 또는 없는 선택에 대한 방어 처리.

**확인할 사항**
- ☐ 탐색기에서 `n` 으로 `/EXPTEST` 생성 → 콘솔 `dir /` 에서 보인다.
- ☐ 탐색기에서 파일 삭제 → 호스트 `bxos_fat.py ls` 에서 사라지고 `fsck_msdos -n build/cmake/data.img` 통과.
- ☐ 비어 있지 않은 디렉터리 삭제는 실패하고 목록이 손상되지 않는다.
- ☐ rename 후 파일 내용이 byte-for-byte 유지된다.
- ☐ QEMU 재부팅 후 탐색기에서 변경 결과가 유지된다.

### Phase 7 — UI polish / 오류 처리 / 성능 정리 (1일)
- ☐ 긴 목록 스크롤 polish:
  - ☑ selection 이 화면 밖으로 나가면 list/tree offset 갱신.
  - ☑ mouse wheel 대체로 tree/list scrollbar 를 제공한다.
  - ☐ PageUp/PageDown 또는 scrollbar arrow 버튼 추가 여부 검토.
- ☑ path/status 가 너무 길면 앞부분 또는 가운데를 `...` 로 줄인다.
- ☐ toolbar icon slot 크기/간격을 고정하고 hover/pressed/disabled 상태를 구분한다.
- ☐ status message timeout 또는 다음 입력 시 clear.
- ☐ 색상 규칙 정리:
  - ☐ tree selection, list selection, directory, executable, general file 을 구분.
  - ☐ 너무 화려하지 않은 16색 팔레트 사용.
- ☐ out-of-memory, handle 부족, path 초과, invalid 8.3 입력을 사용자에게 명확히 표시.
- ☐ code style 을 기존 앱들(`tetris`, `winhelo3`) 과 맞춘다.

**확인할 사항**
- ☐ root 에 25개 이상 파일이 있어도 스크롤과 선택이 안정적이다.
- ☐ path 길이가 128 근처여도 UI 텍스트가 겹치지 않는다.
- ☐ `api_opendir` handle leak 없이 반복 reload 가능.
- ☐ 30회 이상 디렉터리 이동/refresh 후에도 앱이 멈추지 않는다.
- ☐ 마우스 조작과 키보드 조작을 섞어도 focus/selection 이 어긋나지 않는다.

### Phase 8 — 문서 / 회귀 검증 / 마무리 (1일)
- ☐ [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) 에 `explorer` 사용법 추가.
- ☐ [README.utf8.md](../README.utf8.md) 빠른 시작 앱 목록 갱신.
- ☐ [SETUP-MAC.md](../SETUP-MAC.md) 에 explorer 빌드/실행/검증 한 단락 추가.
- ☐ [he2/README.md](../he2/README.md) 또는 [he2/docs/HE2-FORMAT.md](../he2/docs/HE2-FORMAT.md) 에 새 syscall/event API 표 추가 여부 확인.
- ☐ work4.md / work4-handoff.md 체크박스 갱신.

**확인할 사항**
- ☐ clean build: `cmake --build build/cmake` 통과.
- ☐ `fsck_msdos -n build/cmake/haribote.img` / `fsck_msdos -n build/cmake/data.img` 통과.
- ☐ QEMU smoke:
  - ☐ `explorer` 실행.
  - ☐ tree/list 표시.
  - ☐ 키보드/마우스 selection.
  - ☐ 디렉터리 진입/상위 이동.
  - ☐ 창 resize 후 레이아웃 재배치.
  - ☐ 텍스트 preview.
  - ☐ `.he2` 실행.
  - ☐ mkdir/delete/rename 후 재부팅 유지.
- ☐ 기존 콘솔 명령 `dir`, `cd`, `mkdir`, `rmdir`, `cp`, `mv`, `rm`, `type`, `langmode 3/4` 회귀 없음.
- ☐ 기존 HE2 window 앱(`tetris`, `winhelo3`, `lines`) 의 키보드 입력/창 닫기 동작 회귀 없음.

## 4. 마일스톤 / 검증 시나리오

| 끝난 시점 | 검증 |
|---|---|
| Phase 1 | 사용자 앱에서 `/` 를 opendir/readdir 할 수 있고, 기존 콘솔 명령이 그대로 동작한다. |
| Phase 2 | 검증 앱이 mouse click/move 와 resize event 를 받고 redraw 할 수 있다. |
| Phase 3 | `explorer` 창이 뜨고 toolbar/tree/list/status 가 보이며, 키보드/마우스로 root/subdir 목록 이동이 가능하다. |
| Phase 4 | explorer 창 크기 변경 시 tree/list/status 가 비율에 맞게 재배치된다. |
| Phase 5 | 탐색기에서 `.he2` 실행과 텍스트/hex preview 가 가능하다. |
| Phase 6 | 탐색기에서 mkdir/delete/rename 이 가능하고, 호스트 `fsck_msdos` 가 통과한다. |
| Phase 7 | 긴 목록, 긴 path, 반복 refresh, 마우스/키보드 혼합 조작에서 UI/handle 이 안정적이다. |
| Phase 8 | 문서와 빌드 산출물이 최신 상태이며, 기존 work1~3 기능이 회귀하지 않는다. |

## 5. 위험 요소 / 함정

- **사용자 앱 디렉터리 열람 API 부재** — explorer 를 먼저 만들 수 없다. Phase 1 이 선행되어야 한다.
- **앱 mouse event ABI** — 시스템이 처리할 영역(title bar, resize edge, close button) 과 앱 client area 이벤트를 명확히 나누지 않으면 창 이동/닫기와 앱 클릭이 서로 방해한다.
- **앱 window resize 구조** — 앱 버퍼는 사용자 메모리이므로 커널이 임의로 늘릴 수 없다. resize event 후 앱이 새 buffer 를 제공하고 sheet buffer 를 교체하는 절차가 필요하다.
- **TASK 리소스 누수** — dir handle, event state, resize buffer 교체 중 기존 buffer 정리를 앱/커널 어디가 맡는지 명확해야 한다.
- **`api_exec` cwd 상속** — 탐색기에서 실행한 앱이 선택 파일의 디렉터리 또는 탐색기 현재 경로 중 무엇을 cwd 로 볼지 명확해야 한다. 이번 계획은 “탐색기 현재 경로 상속” 으로 고정.
- **tree lazy load 일관성** — 디렉터리 삭제/rename 후 tree node cache 가 stale 해질 수 있다. 파일관리 동작 후 해당 subtree 를 reload 한다.
- **splitter drag 와 resize 충돌** — 사용자가 splitter 를 조절한 비율은 창 resize 후에도 보존하되 최소/최대 폭 clamp 를 적용한다.
- **실행과 파일 preview 의 구분** — `.he2` 확장자 생략 실행과 실제 파일명 표시 규칙이 섞이면 혼란스럽다. 탐색기는 실제 파일명 기준으로 `.HE2` 만 실행한다.
- **rename/move 일관성** — 콘솔 `mv` 는 copy+unlink 기반이다. 같은 디렉터리 rename 을 직접 slot update 로 최적화할지, 기존 raw copy 경로를 쓸지 Phase 1 에서 결정해야 한다.
- **삭제 confirm UI** — 실수 삭제가 쉬우므로 최소한 `y` confirm 은 필요하다.
- **8.3 입력 검증** — 앱 UI에서 긴 이름을 받아도 결국 FAT 8.3 단순 절단이 된다. silent truncation 은 위험하므로 앱에서 미리 경고하거나 거부한다.
- **바이너리 preview** — 임의 바이너리를 문자열로 찍으면 제어문자 때문에 화면이 깨질 수 있다. printable ASCII + hex fallback 이 필요하다.
- **메모리 사용량** — tree nodes, entries 배열, preview buffer, resize buffer 를 1MB 기본 HE2 heap 안에서 제한한다.
- **동시 변경** — 콘솔이나 다른 앱이 같은 디렉터리를 바꿀 수 있다. 탐색기는 `r` refresh 와 실패 시 reload 로 대응한다.

## 6. 범위 외 (이번 작업에서 안 하는 것)

- 긴 파일명(LFN), 한글 파일명, CP949 파일명.
- 다중 선택.
- recursive copy/delete (`cp -r`, `rm -r`).
- 파일 복사 progress UI.
- toolbar 전체 기능 완성. work4 에서는 배치와 일부 기본 기능 연결까지만.
- 이미지 썸네일, 아이콘 리소스, MIME 판별.
- 파일 내용 편집기.
- 디스크 용량 막대 / free cluster 표시.
- 여러 드라이브 prefix (`A:/`, `C:/`).
- 권한, 숨김 파일 정책, timestamp 표시.

## 7. 예상 일정

총 **12~14 작업일** (한 사람 풀타임 기준). Phase 1 의 파일관리 ABI와 Phase 2 의 app mouse/resize ABI가 가장 중요하다. 2-pane UI와 리사이즈 대응까지 포함되므로 기존 키보드-only 계획보다 3~4일 정도 늘어난다.
