# work5 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work5.md](work5.md) 작업을 새 세션에서 이어받기 위한 단일
진입점이다. 먼저 work5.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음
행동을 잡으면 된다.

work1 (쓰기 가능 FAT16) → work2 (서브디렉터리/cwd) → work3 (한글 출력) →
work4 (파일 탐색기) 다음 작업이다.

---

## 1. 한 줄 요약

BxOS 데스크톱에 **Windows 95 스타일 Start 버튼 + Start Menu + 시스템 트레이
시계 + 모던 선언적 Settings 앱** 을 추가한다. 메뉴 트리는 코드가 아니라
`/SYSTEM/MENU.CFG` 의 INI-like 선언으로 정의되고, 설정은 스키마 선언에서
자동 생성되는 UI 가 다룬다. 변경값은 `/SYSTEM/SETTINGS.CFG` 에 저장된다.

## 2. 현재 위치 (2026-05-01 기준)

- work5 는 **Phase 0 완료 → Phase 1 진입 직전** 단계다. 코드 변경은 아직 없다.
- Phase 0 에서 잠근 핵심 (work5.md §3 Phase 0 표 참조):
  - 신규 사용자 syscall **0개**. 모든 widget(taskbar/menu/시계/Run/About) 은 커널 내부.
    Settings 앱은 기존 file syscall(28~30) 로 SETTINGS.CFG read/write.
  - `/SYSTEM/MENU.CFG`, `/SYSTEM/SETTINGS.CFG` 경로 / INI-like 포맷 확정.
  - 핸들러 URI: `exec:` / `builtin:` / `settings:` / `submenu:` 4종.
    빌트인 id: `console`/`taskmgr`/`run`/`about`/`shutdown`/`restart`/`settings`.
  - 1차 settings key 4 카테고리 8개 — `display.background`, `language.mode`,
    `time.boot_offset_min`, `time.show_seconds`, `about` (+ Display/Language/Time/About 카테고리 액션).
  - Settings 적용 정책: SETTINGS.CFG 즉시 저장, 시스템 변수는 **다음 부팅부터** 적용
    (Settings 앱 안 미리보기는 즉시).
  - 키보드: 기존 `key_shift` 옆에 `key_ctrl`/`key_alt` 비트마스크 추가 (Phase 1).
    기존 Tab→window-cycle 은 Phase 6 에서 제거하고 Alt+Tab 으로 옮김.
  - `/SYSTEM/` install 은 mkfat12.py(root flat only) 다음에 bxos_fat.py 로 mkdir+cp (Phase 3).
- 충돌 검증 (work2/3/4):
  - 새 이름들이 모두 8.3 적합, data.img 의 기존 40 entries 와 충돌 없음 (root_entries=512 한계 충분).
  - `langmode N` 콘솔 명령은 그대로(현재 task scope).
  - work4 syscall 32~43 보존, `api_exec` cwd/subsystem 정책 그대로 재사용.
- 부팅 후 화면 하단 28px 자리에 win95 톤 회색 taskbar 가 이미
  `init_screen8()` 으로 그려져 있다 — 좌측 60×20 ‘Start 자리’ 와 우측 44×20
  ‘트레이 자리’ 가 비어 있다. Phase 1 에서 이 두 영역을 실제 widget 으로 만든다.
- 기존 GUI 인프라:
  - `sheet.c` 의 z-order, mouse hit-test, refresh API 가 system sheet 까지 다 다룬다.
  - `mouse.c` / `keyboard.c` → `bootpack.c` HariMain 의 mouse/keyboard loop.
  - 앱 윈도우는 `api_openwin` 으로 만들고, mouse/resize event 는 work4 에서 추가됨.
- 기존 콘솔 / 앱:
  - 콘솔 명령: dir, cd, mkdir, rmdir, cp, mv, rm, type, langmode 0~4, mem, task,
    taskmgr, disk, start, ncst.
  - HE2 앱 28개. work5 에서 Notepad/Calc/Settings 3개 정도 추가 예정.
- 미존재:
  - Start 버튼/메뉴, system tray 시계, taskbar window list, Alt+Tab.
  - GUI 설정 UI, 영구 저장 메커니즘.
  - `/SYSTEM/` 서브디렉터리.

## 3. 확정할 핵심 결정

work5.md §2 가 정본이다. 요약:

- Start 버튼은 기존 taskbar 좌측 60×20 자리에. Ctrl+Esc 로도 토글.
- 메뉴는 위로 펼치고, sub-menu 는 오른쪽으로. 200px 폭, 22px 항목.
- 메뉴 구조 = `/SYSTEM/MENU.CFG` (INI-like 선언). 부팅 시 1회 적재.
- 핸들러 URI: `exec:<path>` / `builtin:<id>` / `settings:<key>` / `submenu:<section>`.
- 빌트인 id: `console`, `taskmgr`, `run`, `about`, `shutdown`, `restart`.
- Settings 앱은 `settings.he2`. 좌측 카테고리, 우측 자동 생성 페이지.
  스키마(`type` enum/bool/int/str/action) 가 위젯을 결정.
- 1차 카테고리: Display, Language, Time, About.
- 영구 저장: `/SYSTEM/SETTINGS.CFG` 에 즉시 기록.
- 시계: 60s timer + 메뉴 토글 시 redraw. RTC 없음 → 부팅 시각 0:00, Settings 에서 set.
- Alt+Tab 으로 system sheet 제외하고 visible app sheet 순환.
- 신규 사용자 syscall 은 추가하지 않는다 — 모두 커널 내부 / 기존 syscall 로 해결.

## 4. 제안 파일 / 자료 구조

### `/SYSTEM/MENU.CFG` (예시)

```ini
[start]
items = Programs, Settings, ---, Run..., About BxOS, ---, Restart, Shutdown

[start/Programs]
items = Explorer, Console, Notepad, Calc, Games, ---, Task Manager

[start/Programs/Games]
items = Tetris

[item:Programs]      ; submenu link
handler = submenu:start/Programs

[item:Settings]      ; opens settings.he2
handler = builtin:settings

[item:Run...]
handler = builtin:run
shortcut = Ctrl+R

[item:About BxOS]
handler = builtin:about

[item:Restart]
handler = builtin:restart

[item:Shutdown]
handler = builtin:shutdown

[item:Explorer]
handler = exec:/EXPLORER.HE2

[item:Console]
handler = builtin:console

[item:Notepad]
handler = exec:/NOTEPAD.HE2

[item:Calc]
handler = exec:/CALC.HE2

[item:Tetris]
handler = exec:/TETRIS.HE2

[item:Task Manager]
handler = builtin:taskmgr
```

### `/SYSTEM/SETTINGS.CFG` (예시)

```ini
# 변경 시 settings.he2 가 자동으로 다시 기록한다.
display.background = navy
language.mode = 0
time.show_seconds = false
```

### Settings 스키마 (settings.c 내 const)

```c
enum { SET_TYPE_ENUM, SET_TYPE_BOOL, SET_TYPE_INT, SET_TYPE_STR, SET_TYPE_ACTION };
struct SettingChoice { const char *value; const char *label; };
struct SettingSpec {
    const char *category;     /* "display" / "language" / "time" / "about" */
    const char *key;
    int type;
    const char *label;
    const char *help;
    const struct SettingChoice *choices;
    int n_choices;
    int int_min, int_max;
    const char *default_value;
    int apply_immediately;     /* 0 = 재시작 권장, 1 = 즉시 */
};
```

### Menu / MenuItem in-memory

```c
enum { HANDLER_NONE, HANDLER_EXEC, HANDLER_BUILTIN, HANDLER_SETTINGS, HANDLER_SUBMENU };
struct MENU_ITEM {
    char  label[24];
    int   handler_kind;
    int   builtin_id;          /* HANDLER_BUILTIN: console/taskmgr/run/about/shutdown/restart */
    char  handler_arg[64];     /* exec path, settings key, submenu section */
    int   flags;               /* SEPARATOR | DISABLED | HAS_SUBMENU */
};
struct MENU {
    struct SHEET     *sht;
    struct MENU_ITEM  items[64];
    int               n_items, selected;
    struct MENU      *parent, *child;
};
```

## 5. Phase 한눈에 보기

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 요구사항 / 인터페이스 확정 ☑ | 0.5d | Start hit zone, MENU.CFG 포맷, 핸들러 URI, settings 카테고리 4개 잠금 (2026-05-01 완료) |
| 1. Taskbar / Start 버튼 / tray | 2d | sht_back hit-test, hover/press, tray placeholder, Ctrl+Esc 합성 |
| 2. Menu sheet primitive | 1.5d | menu.c, cascading sub-menu, click-outside-close, keyboard nav |
| 3. MENU.CFG loader | 1.5d | `/SYSTEM/` 디렉터리, INI-like parser, 빌트인 default fallback |
| 4. 핸들러 / 시계 / Run / About | 1.5d | exec/builtin dispatcher, 60s timer 시계, Run/About modal |
| 5. Settings 앱 | 2d | settings.he2, 스키마 자동 위젯, SETTINGS.CFG 영구 저장 |
| 6. Taskbar 윈도우 목록 / Alt+Tab | 1d | 가운데 영역 버튼, focus 토글, Alt+Tab 순환 |
| 7. UI polish / 오류 처리 | 1d | underline letter, win95 톤, modal focus, 파싱 에러 |
| 8. 문서 / 회귀 검증 | 1d | BXOS-COMMANDS / README / SETUP / he2-format 갱신, QEMU smoke |

총 10~12 작업일 예상.

## 6. 코드 길잡이

| 영역 | 볼 파일 |
|---|---|
| 화면 / taskbar 그리기 | [harib27f/haribote/graphic.c](../harib27f/haribote/graphic.c) `init_screen8` |
| 부팅 진입 / mouse·keyboard loop | [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) `HariMain` |
| sheet z-order / hit test | [harib27f/haribote/sheet.c](../harib27f/haribote/sheet.c) |
| window 그리기 | [harib27f/haribote/window.c](../harib27f/haribote/window.c) |
| 콘솔 task / cmd_app | [harib27f/haribote/console.c](../harib27f/haribote/console.c) `console_task`, `cmd_app`, `cmd_start` |
| 마우스 커서 / 모양 | [harib27f/haribote/mouse.c](../harib27f/haribote/mouse.c) |
| 키보드 변환 | [harib27f/haribote/keyboard.c](../harib27f/haribote/keyboard.c) |
| 타이머 (시계용) | [harib27f/haribote/timer.c](../harib27f/haribote/timer.c) |
| 파일 read/write | [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c) |
| HE2 앱 빌드 | [he2/cmake/HE2App.cmake](../he2/cmake/HE2App.cmake), [CMakeLists.txt](../CMakeLists.txt) |
| user wrapper (settings.he2 가 사용) | [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h), [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) |
| 윈도우 앱 예시 | [harib27f/explorer/explorer.c](../harib27f/explorer/explorer.c), [harib27f/tetris/tetris.c](../harib27f/tetris/tetris.c) |

## 7. 빠른 빌드/실행 치트시트

```bash
cmake -S . -B build/cmake
cmake --build build/cmake
./run-qemu.sh
```

work5 구현 후 예상 QEMU 동작:

```text
< click Start 버튼 또는 Ctrl+Esc >
> Programs > Explorer
> Settings > Language > UTF-8
> Run... > "explorer /sub" > Enter
> About BxOS
> Shutdown
```

호스트 검증:

```bash
fsck_msdos -n build/cmake/data.img
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/SYSTEM
python3 tools/modern/bxos_fat.py cat build/cmake/data.img:/SYSTEM/MENU.CFG
```

## 8. 바로 시작할 때 할 일

1. `git status --short` — 작업트리 깨끗한지 확인.
2. ~~Phase 0 잠금~~ — 완료 (2026-05-01). work5.md §3 Phase 0 표가 정본.
3. **Phase 1 진입** — `init_screen8` 옆에 `taskbar_redraw(state)` 헬퍼 추가,
   `HariMain` mouse loop 에 taskbar hit-test, `key_ctrl`/`key_alt` 비트마스크 추가 +
   Ctrl+Esc 합성 (Start 클릭 이벤트와 동일 경로). 이 단계에서는 메뉴는 아직 안
   띄움 (Phase 2). 신규 globals (`g_background_color` 초기값 = `COL8_008484`,
   `g_start_menu_open`) 도 Phase 1 에서 선언하고 `init_screen8` 이 그것을 읽도록 수정.
4. Phase 2 의 menu.c 를 먼저 임시 hard-coded items 로 띄워 동작 확인 → Phase 3 의 MENU.CFG loader 로 교체.
5. Phase 3 에서 mkfat12.py 가 flat root 만 지원하는 점에 유의:
   add_custom_command 의 mkfat12 호출 뒤에 bxos_fat.py 로 `mkdir /SYSTEM` + `cp <MENU.CFG> /SYSTEM/MENU.CFG` 단계 추가.
   `BXOS_SYSTEM_FILES` 라는 새 list 변수에 `_doc/system/menu.cfg.default`,
   `_doc/system/settings.cfg.default` 같은 default 본 경로를 모은다.
6. Phase 6 에서 기존 Tab→window-cycle (`bootpack.c:288`) 을 제거하고 Alt+Tab 으로 옮길 때,
   explorer Tab(tree↔list focus) 와 tetris Tab(미사용) 회귀를 같이 검증.
7. Settings 카테고리/키 추가 시 work5.md §3 Phase 5 의 spec 표 한 줄과 `g_settings[]` 한 줄을 같이 갱신.

## 9. 함정으로 미리 알아둘 것

- **system sheet 가 늘어난다**: cursor / debug / taskbar / Start menu / sub-menu / Run modal / About modal. z-order 순서가 헷갈리지 않도록 phase 별로 항상 “cursor 가 가장 위, taskbar 는 가장 아래의 system layer” 규칙을 지킨다.
- **키보드 단축키 충돌**: Ctrl+Esc, Alt+Tab, Ctrl+R 가 기존 콘솔/앱 동작과 충돌하지 않게 keyboard.c 분기 확인. 특히 tetris 같은 앱에 들어가는 키 코드와 비교.
- **MENU.CFG 누락 시 fallback**: 빌트인 default menu (Explorer / Console / Settings / Run / About / Shutdown) 가 코드 안에 있어야 부팅 가능.
- **SETTINGS.CFG 쓰기 실패 처리**: 디스크 가득/handle 부족 시 status 표시 + 메모리 값은 그래도 적용.
- **Alt + Modal**: Run / About 떠 있는 동안 Alt+Tab 은 무시 (또는 modal 안에서만 순환).
- **시계 redraw 주기**: 1Hz 는 과해서 60s 로 잡고, 메뉴/Settings 토글 시 즉시 1회 redraw 한다. Settings 의 `time.show_seconds = true` 로 바꾸면 1Hz 로 변경.
- **languages.mode 즉시 적용 한계**: 이미 떠 있는 콘솔의 langmode 는 그대로. spec 의 `apply_immediately = 0` (재시작 권장) 로 표시.
- **`api_exec` cwd 정책 재사용**: Run 다이얼로그와 메뉴 `exec:` 핸들러 둘 다 work4 의 “실행 파일 부모를 cwd 로 상속” 정책을 그대로 쓴다.
- **MENU.CFG 의 `---`**: separator 는 `items = …, ---, …` 에서만 의미. `[item:---]` 같은 건 무시.
- **메뉴 sheet 의 close-and-dispatch**: 외부 click 으로 메뉴를 닫을 때, 같은 click 을 일반 sheet hit-test 로 한 번 더 흘려보낸다(Phase 2 §click-outside).
- **HE2 앱 추가 (Notepad/Calc)**: work5 본문 범위에 들어가는 Notepad/Calc 의 기능 한계는 일부러 작게 잡는다 — 4-함수 Calc, 8KB 까지 read-only 가능한 Notepad. 본격 편집기는 work6.

## 10. 작업하지 말아야 할 것

- 데스크톱 아이콘 / drag&drop.
- 테마 / 팔레트 교체 / 다국어 자동 번역.
- 다중 사용자 / 로그인 / 권한.
- 사운드 mixer / 네트워크 설정.
- 정확한 RTC / NTP.
- 윈도우 애니메이션, fade, transparency.
- Start menu 검색 박스 / 자주 쓰는 앱 추천.
- Settings spec 의 동적 등록 (앱이 자기 설정을 추가하는 plugin 시스템).
