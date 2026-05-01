# work5 — 시작 버튼 / Start Menu / 시스템 설정 계획

## 1. 배경 / 목표

### 현재 상태 (work4 종료 시점)
- 부팅 후 화면 하단에 Windows 95 스타일의 **회색 taskbar 영역(28px)** 이
  `init_screen8()` 으로 그려져 있다 — 왼쪽에는 빈 “Start” 자리(약 60×20px),
  오른쪽에는 빈 시스템 트레이 자리(약 44×20px). 그러나 어느 쪽도 클릭/그리기
  핸들이 연결되어 있지 않다.
- 윈도우 앱은 explorer / tetris / winhelo3 / lines / evtest 등이 있고,
  콘솔 앱은 lsdir / pwd / chklang / type / sosu 등이 있다. 모든 앱은
  콘솔에서 이름으로 실행하거나 `start <name>` / `ncst <name>` 으로 새 콘솔에서
  띄울 수 있다. 탐색기에서 `.HE2` 를 Enter / double-click 으로 실행하는
  `api_exec` 도 있다 (work4 Phase 5/8).
- 마우스/키보드 이벤트는 **창 단위로** 라우팅된다. taskbar 영역(=`sht_back`)
  은 sheet 이지만 마우스 클릭 dispatch 가 없고, 시스템 단축키
  (Ctrl+Esc / Alt+Tab / Win) 도 처리되지 않는다.
- 앱 설정 / 환경 설정은 **콘솔 명령(`langmode 0~4`) 으로만** 가능하다.
  GUI 에서 설정을 바꿀 통로가 없다.
- 데이터 디스크 `data.img` 의 루트에는 앱과 데이터 파일만 있고,
  **시스템 구성을 위한 별도 디렉터리/파일이 없다**. 모든 정책은 코드에 박혀 있다.

### 이번 작업의 목표
1. **Start 버튼 + Start Menu** 를 추가한다.
   - taskbar 좌측 60×20 영역을 실제 클릭/hover 가능한 버튼으로 만든다.
   - 클릭 또는 Ctrl+Esc 입력 시 위로 펼쳐지는 메뉴 sheet 가 뜬다.
   - 메뉴는 cascading sub-menu (오른쪽으로 펼치기) 와 separator, label 를 지원한다.
   - 키보드(↑/↓/←/→/Enter/Esc) 와 마우스(hover/click/click-outside-to-close) 모두 동작.
2. **모던 configure 설정 방법으로 메뉴를 선언**한다.
   - 메뉴 구조와 항목을 코드 대신 **선언적 설정 파일** (`/SYSTEM/MENU.CFG`) 로 기술.
   - 항목은 “이름”, “핸들러 URI”, (선택) “단축키”, (선택) “icon hint” 로 표현.
   - 핸들러 URI 는 `exec:<path>`, `builtin:<id>`, `settings:<key>`, `submenu:<section>`
     네 가지로 한정한다 (스키마 단순/검증 가능).
   - 부팅 시 1회 파싱해 메뉴 트리로 적재. 파싱 실패 항목은 status 로 표시하고 skip.
3. **기본 필수 애플리케이션** 을 등록한다.
   - 파일관리: Explorer.
   - 콘솔: Console (새 콘솔 윈도우).
   - 텍스트: Notepad (간이 뷰어/편집기 — 신규).
   - 계산기: Calc (단순 사칙연산 + 16색 LCD 풍 — 신규).
   - 게임: Tetris.
   - 시스템: Task Manager, Run…, About BxOS.
   - Shutdown / Restart (커널 sleep + label).
4. **환경 설정 (Settings) UI** 를 추가한다.
   - “Settings” 라는 통합 앱 1 개 (`settings.he2`) 가 모든 설정을 다룬다.
   - 좌측에 카테고리 트리, 우측에 카테고리 페이지 (modern Settings 앱과 동일한 스플릿).
   - 각 설정은 **스키마 선언** 으로 기술된다. 스키마(`type`, `choices`, `default`, `label`)
     로부터 위젯이 자동 생성된다 (enum/int/bool/string/action).
   - 변경 시 즉시 반영(시스템 변수 갱신) + `/SYSTEM/SETTINGS.CFG` 에 영구 저장.
5. **windows 시스템에 준하는 기본 구성요소** 를 추가한다 (이번 work5 범위 내).
   - taskbar 의 시스템 트레이에 **HH:MM 시계**.
   - taskbar 에 **실행 중 윈도우 목록 버튼** (focus toggle / minimize).
   - **Run… 다이얼로그** (모달 입력창 → `cmd_app` 와 동일 경로로 실행).
   - **Alt+Tab** focus 전환, **Ctrl+Esc** Start Menu 토글.
   - **About** 시스템 정보 다이얼로그 (커널 빌드 / 메모리 / 화면 해상도 / langmode).

## 2. 설계 결정 사항 (계획안 — 2026-05-01)

| 항목 | 결정 | 비고 |
|---|---|---|
| Start 버튼 위치 | taskbar 좌측 (x 2..60, y `scrny-24..scrny-4`) | 기존 `init_screen8()` 의 빈 자리 그대로 사용. |
| 시스템 트레이 위치 | taskbar 우측 (x `scrnx-47..scrnx-3`) | 기존 빈 자리 사용. 시계 + langmode indicator. |
| Start Menu 폭/높이 | 폭 약 200px, 항목당 22px, 최대 높이 = 화면 높이 - 30 | 화면 작아지면 스크롤 도입은 work5 범위 외, 잘림. |
| 펼침 방향 | 위로 (taskbar 위) | 화면 하단 고정 taskbar 와 일관. |
| 메뉴 sheet 구현 | 새 system sheet (key_win 과 별도 z-order), focus 변경 없음 | 메뉴가 떠 있는 동안에는 Ctrl+Esc / Esc / 외부 click 으로 닫음. |
| 메뉴 구성 파일 | `/SYSTEM/MENU.CFG` | data.img 루트에 `SYSTEM/` 서브디렉터리. Phase 0 에서 생성 정책 확정. |
| 메뉴 구성 형식 | INI-like, UTF-8, 줄당 1 항목 | (예시는 §3 Phase 3.) JSON/TOML 은 파서 부담으로 미채택. |
| 핸들러 URI scheme | `exec:<path>` / `builtin:<id>` / `settings:<key>` / `submenu:<section>` | 파싱 후 enum 으로 normalize. |
| 빌트인 핸들러 id | `console`, `taskmgr`, `run`, `about`, `shutdown`, `restart` | 추가는 코드+문서+config 동시 갱신. |
| 단축키 모델 | Ctrl+Esc (Start), Alt+Tab (focus 전환), Esc (메뉴 닫기), 항목 underline 글자 | underline 는 Phase 5 polish. |
| 메뉴 항목 사이즈 | 22px height, 16px font, 4px 좌우 padding, 4px sub-arrow | sub 는 ▶ 문자. |
| 키보드 focus 정책 | 메뉴 열림 → 메뉴가 키보드 점유, 닫히면 이전 key_win 복원 | 기존 `set_keywin()` 로 복원. |
| 마우스 click-outside | menu sheet 외부 클릭 시 닫음 + 같은 click 을 일반 dispatch | 클릭 좌표 hit-test 후 두 단계. |
| Settings 앱 | `settings.he2` (HE2 window) 1 개 | 카테고리 트리 + 페이지. |
| Settings 스키마 위치 | 빌트인 (settings.c 안의 const struct array) | 사용자 설정 값은 `/SYSTEM/SETTINGS.CFG` 에 저장. |
| 위젯 종류 | enum(radio/dropdown), bool(checkbox), int(slider), string(line input), action(button) | int 슬라이더는 Phase 5 polish — 1차는 `+`/`-` 버튼. |
| 카테고리 (1차) | Display, Language, Time, About | Sound/Mouse 는 Phase 6 또는 work6. |
| 영구 저장 | 변경 즉시 `SETTINGS.CFG` 재기록 | 부팅 시 read-back. write 실패 시 status 표시. |
| 시계 갱신 주기 | 1분 (60s timer) + 메뉴 토글 시 즉시 redraw | RTC 가 없으므로 부팅 시각 + uptime. |
| 시계 표기 | `HH:MM` (24h) | langmode UTF-8 일 때도 ASCII 만 사용. |
| Run… 다이얼로그 | 시스템 모달 sheet (단일 line input + OK/Cancel) | 입력은 `cmd_app(NULL, fat, cmdline)` 또는 신규 `sys_run()` 으로 실행. |
| Run 실행 방식 | 항상 새 콘솔 (= cmd_start 와 동일) | window 앱은 콘솔 없이, console 앱은 콘솔 윈도우 같이. work4 api_exec 정책 재사용. |
| Alt+Tab | 시스템 sheet z-order 상위 → 다음 visible app sheet 으로 focus 이동 | system sheet (taskbar/menu/cursor) 는 skip. |
| 윈도우 task list 버튼 | taskbar 가용폭(`60..scrnx-50`) 을 균등 분할, 최대 8개 | 8개 초과는 “…” 잘림. |
| 종료 동작 (`builtin:shutdown`) | “이제 컴퓨터를 꺼도 안전합니다” label 표시 + cli 후 hlt | QEMU `-no-shutdown` 동작 보존. |
| 재시작 (`builtin:restart`) | `int 0x19` (BIOS reboot) 시도, 실패 시 keyboard reset | 1차는 BIOS reboot 만. |
| 색상 팔레트 | 기존 16색 그대로 | win95 톤(silver, navy, white, dark gray)을 4~5색에 매핑. |
| 메뉴 사이즈 한계 | 항목 64개, 카테고리당 32개, 메뉴 깊이 3 | 정적 배열로 메모리 부담 묶음. |
| 사용자 syscall 추가 여부 | **없음** | menu / taskbar / settings / clock 은 모두 커널 측. settings.he2 는 기존 syscall 만 사용. |
| 메뉴 파일 인코딩 | ASCII (한글 라벨은 EUC-KR) | langmode 4(UTF-8) 에서도 라벨 출력은 langmode-aware text path 사용. |

## 3. 작업 단계

각 Phase 끝의 ☐ 는 PR/커밋 단위 자연 경계.

### Phase 0 — 요구사항 / 인터페이스 확정 (0.5일)
- ☐ 본 문서와 [work5-handoff.md](work5-handoff.md) 를 정본으로 두고 MVP 범위 잠금.
- ☐ 다음 결정을 잠근다:
  - Start 버튼 hit zone, Start Menu 펼침 방향, 키보드 단축키.
  - 메뉴 / 설정 config 파일 경로(`/SYSTEM/MENU.CFG`, `/SYSTEM/SETTINGS.CFG`),
    포맷(INI-like), 핸들러 URI scheme, 빌트인 id 셋.
  - 1차 카테고리(Display/Language/Time/About) 와 각 setting key list.
  - 1차 메뉴 트리 (§3 Phase 3 예시 그대로).
- ☐ 범위 외 명시: 데스크톱 아이콘, drag&drop, theming engine, 다중 사용자,
  로그인 화면, 시스템 트레이 plugin, 사운드 mixer, 네트워크 설정.
- ☐ work2/work3/work4 와 충돌 없음 확인 (langmode 정책, FAT path 규칙, syscall 번호).

**Phase 0 잠금 결정 요약 (변경 시 work5.md/work5-handoff.md 동시 갱신)**

| 잠금 항목 | 값 |
|---|---|
| Start 버튼 hit zone | taskbar 좌측 (x 2..60, y `scrny-24..scrny-4`) |
| Start Menu sheet 폭 | 200px, 항목 22px, 위로 펼침 |
| Start 토글 단축키 | Ctrl+Esc / Esc(닫기) |
| Alt+Tab focus 순환 | system sheet 제외, visible app sheet 만 |
| 메뉴 config 경로 | `/SYSTEM/MENU.CFG` (data.img) |
| 설정 영구 저장 경로 | `/SYSTEM/SETTINGS.CFG` |
| 핸들러 URI | `exec:` / `builtin:` / `settings:` / `submenu:` |
| 빌트인 id | `console`, `taskmgr`, `run`, `about`, `shutdown`, `restart` |
| 1차 settings 카테고리 | `display`, `language`, `time`, `about` |
| 1차 settings key | `display.background`(enum), `language.mode`(enum), `time.show_seconds`(bool), `about` (read-only action) |
| 시계 갱신 주기 | 60s timer, 메뉴 토글 시 즉시 redraw |
| 메모리 한계 | 메뉴 항목 64, 카테고리당 32, 깊이 3 |
| 신규 syscall | 없음 (커널 sheet/menu/clock 모두 내부 처리) |

### Phase 1 — Taskbar / Start 버튼 / 시스템 트레이 (커널 widget) (2일)
**목표**: 기존 `sht_back` taskbar 영역을 실제 클릭 가능한 버튼/트레이로 만든다.

- ☐ [bootpack.c](../harib27f/haribote/bootpack.c) HariMain 의 mouse loop 에 taskbar 클릭 hit-test 추가.
  - hit zone: y >= `scrny-28`. 좌측 60×20 = Start 버튼, 우측 44×20 = tray.
  - 버튼 영역에 hover/pressed 상태가 시각적으로 보이도록 `boxfill8` redraw.
- ☐ Start 버튼 라벨 그리기 (가능하면 “🪟 Start” 대신 “Start” 텍스트 + 이중선 시뮬).
- ☐ 시스템 트레이 시계 placeholder (Phase 4 에서 실제 시간 갱신).
- ☐ Ctrl+Esc 가 들어오면 “Start 버튼 클릭” 과 동일한 이벤트로 합성.
- ☐ taskbar 의 가운데 영역(60..scrnx-50)은 **빈 영역으로 둠** — Phase 6 에서 윈도우 task list 채움.

**확인할 사항**
- ☐ build 통과, `fsck_msdos -n` 통과.
- ☐ Start 버튼 hover/press 가 시각적으로 드러난다.
- ☐ 기존 콘솔/explorer/tetris 등 앱의 mouse 동작 회귀 없음.

### Phase 2 — Menu sheet primitive (1.5일)
**목표**: cascading sub-menu 를 지원하는 일반 메뉴 sheet 구성요소를 추가한다.

- ☐ 신규 `harib27f/haribote/menu.c` (또는 `window.c` 하단 확장):
  - `struct MENU_ITEM { char label[24]; int handler_id; int submenu; int flags; }`.
  - `struct MENU { struct SHEET *sht; struct MENU_ITEM items[..]; int selected; struct MENU *child; struct MENU *parent; }`.
  - `menu_open(parent, items, n, x, y)`, `menu_close(menu)`, `menu_select_next/prev`, `menu_invoke(menu)`.
- ☐ separator / disabled / submenu marker 그리기.
- ☐ keyboard navigation (↑/↓/Enter/Esc/←/→).
- ☐ mouse hover → highlight (이전 hover 항목 redraw), click → invoke.
- ☐ click-outside → root menu 까지 모두 close.
- ☐ ESC → 가장 안쪽 메뉴만 close, root 가 닫히면 focus 복귀.
- ☐ menu sheet z-order 는 cursor 바로 아래.

**확인할 사항**
- ☐ build 통과.
- ☐ 임시 fixed menu 로 Phase 1 의 Start 버튼에서 메뉴를 띄워 동작 확인.

### Phase 3 — 메뉴 config 파일 / loader (1.5일)
**목표**: 메뉴 구조를 **`/SYSTEM/MENU.CFG`** 에서 읽어 빌트인 메뉴 트리로 변환한다.

- ☐ `data.img` 빌드 시 `/SYSTEM/` 서브디렉터리 생성, `MENU.CFG` 와 (Phase 5에서) `SETTINGS.CFG` 의 default 본을 install.
  - CMake `BXOS_DATA_IMG_FILES` 또는 별도 `BXOS_SYSTEM_FILES` 리스트에 추가.
- ☐ menu.cfg 포맷 (예시):

```ini
# /SYSTEM/MENU.CFG — Start Menu 트리. 줄 시작 # 은 주석.
# 형식: <섹션>의 [items] 키는 표시 순서대로 나열.
# 각 항목은 같은 섹션 안에 [item:<이름>] 으로 다시 등장하고, handler 와 옵션을 정의.

[start]
items = Programs, Settings, ---, Run..., About BxOS, ---, Restart, Shutdown

[start/Programs]
items = Explorer, Console, Notepad, Calc, Games, ---, Task Manager

[start/Programs/Games]
items = Tetris

[item:Programs]            ; submenu link only
handler = submenu:start/Programs

[item:Settings]
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

- ☐ loader (`menu_loader.c`):
  - 한 줄씩 읽어 section header / key=value / `---`(separator) 파싱.
  - whitespace trim, 주석 제거, comma split.
  - 알 수 없는 핸들러는 status “invalid menu entry” 로 표시 + skip.
  - 결과: `struct MENU` 트리.
- ☐ 부팅 시 1회 적재 (data.img mount 후, key_win 생성 전).
- ☐ Settings 카테고리는 Phase 5 에서 `submenu:settings` 로 연결되도록 빌트인 entry point 예약.

**확인할 사항**
- ☐ build 통과, `fsck_msdos -n` clean, `bxos_fat.py ls /SYSTEM/` 에 `MENU.CFG` 보임.
- ☐ MENU.CFG 의 한 줄을 일부러 망가뜨리고 부팅 → 그 항목만 빠지고 나머지 메뉴는 정상.

### Phase 4 — 핸들러 / 시계 / Run… / About (1.5일)
**목표**: 메뉴 항목 클릭 시 실제 동작을 연결하고, 트레이 시계와 Run / About 다이얼로그를 추가한다.

- ☐ 핸들러 dispatcher:
  - `exec:<path>` → 기존 `cmd_app` 경로 재사용 (subsystem 분기는 work4 api_exec 와 동일).
  - `builtin:console` → `open_console(shtctl, memtotal)`.
  - `builtin:taskmgr` → 기존 `open_taskmgr()`.
  - `builtin:run` → Phase 4 신규 modal Run 다이얼로그.
  - `builtin:about` → Phase 4 신규 About 다이얼로그.
  - `builtin:shutdown` / `builtin:restart` → §2 의 결정대로.
  - `submenu:<section>` → `menu_open` 자식 메뉴.
  - `settings:<key>` → settings.he2 를 `<key>` 인자와 함께 실행 (Phase 5 에서).
- ☐ 시계: 60s timer. PIT timer 큐의 별도 slot 1개 예약.
  - “HH:MM” 포맷 (24h). RTC 가 없으므로 부팅 시각 + uptime, 부팅 시각은 0:00 으로 가정 (Phase 5 Time 카테고리에서 사용자 set).
  - 메뉴 토글 시점에도 즉시 redraw.
- ☐ Run… 다이얼로그:
  - 시스템 modal sheet, 폭 320 / 높이 100. 한 줄 line input + OK/Cancel.
  - OK → cmd_start 와 동일 경로. 빈 입력은 무시.
  - 다이얼로그 떠 있는 동안 키보드 focus 점유, 메뉴는 닫혀 있어야 함.
- ☐ About BxOS:
  - 시스템 modal sheet, 폭 280 / 높이 160. 텍스트 6줄: 이름, 버전, 빌드 일자,
    화면 해상도, 메모리, 현재 langmode.
  - OK 또는 Esc 로 닫음.

**확인할 사항**
- ☐ build 통과.
- ☐ Start → Run… → `explorer` 입력 → 새 explorer 창.
- ☐ Start → About → 시스템 정보가 정확.
- ☐ Start → Programs → Games → Tetris → 정상 실행 (콘솔 없이).
- ☐ Start → Programs → Console → 새 콘솔 창.
- ☐ tray 시계가 분 단위로 갱신.

### Phase 5 — Settings 앱 (선언적 스키마 기반) (2일)
**목표**: 모던 Settings 앱처럼 선언으로부터 자동 생성되는 설정 UI 를 만든다.

- ☐ 새 HE2 window 앱 `harib27f/settings/settings.c` (`SETTINGS.HE2`).
- ☐ 스키마 (settings.c 내 `static const`):

```c
enum { SET_TYPE_ENUM, SET_TYPE_BOOL, SET_TYPE_INT, SET_TYPE_STR, SET_TYPE_ACTION };
struct SettingChoice { const char *value; const char *label; };
struct SettingSpec {
    const char *category;     /* "display" / "language" / ... */
    const char *key;           /* "display.background" */
    int type;
    const char *label;
    const char *help;
    const struct SettingChoice *choices; int n_choices;
    int int_min, int_max;
    const char *default_value;
};
static const struct SettingSpec g_settings[] = {
    { "display",  "display.background", SET_TYPE_ENUM, "Background color", ...,
      (struct SettingChoice[]){ {"navy","Navy"}, {"black","Black"},
                                {"gray","Gray"}, {"green","Green"} }, 4,
      0, 0, "navy" },
    { "language", "language.mode",      SET_TYPE_ENUM, "Language mode", ...,
      (struct SettingChoice[]){ {"0","ASCII"}, {"1","EUC-JP"}, {"2","SJIS"},
                                {"3","EUC-KR"}, {"4","UTF-8"} }, 5,
      0, 0, "0" },
    { "time",     "time.show_seconds",  SET_TYPE_BOOL, "Show seconds", ...,
      0, 0, 0, 0, "false" },
    { "about",    "about",              SET_TYPE_ACTION, "About BxOS", ...,
      0, 0, 0, 0, 0 },
};
```
- ☐ 좌측 카테고리 리스트, 우측 페이지. 페이지는 해당 카테고리 entry 만 그린다.
- ☐ 위젯 자동 생성:
  - ENUM → 원형 라디오 + 라벨.
  - BOOL → 체크박스.
  - INT → `[ - ] N [ + ]` 버튼.
  - STR → 한 줄 input.
  - ACTION → 버튼 (현재는 About 만).
- ☐ 값 변경 즉시:
  - 시스템 변수 갱신 (`language.mode` → kernel 의 current task->langmode 정책 변경 메시지 또는
    syscall, 시각적 효과만 반영) — 1차는 “재시작 시 적용” 으로 명시 가능한 항목과
    “즉시 적용” 항목을 spec 에 표기.
  - `/SYSTEM/SETTINGS.CFG` 재기록 (`api_fopen_w` + `api_fwrite`).
  - 실패 시 status “save failed”.
- ☐ 부팅 시 SETTINGS.CFG 를 읽어 spec 의 default 를 덮어쓴 뒤 시스템에 적용.
  - `display.background` → `init_screen8` 에 사용할 색 변수.
  - `language.mode` → 부팅 직후 task A 의 langmode.
  - `time.show_seconds` → tray 시계 포맷.
- ☐ 카테고리 트리는 현재 4개 → 추가 시 `g_settings[]` 에만 한 줄 추가.

**확인할 사항**
- ☐ build 통과, `bxos_fat.py ls /` 에 `SETTINGS.HE2` 보임.
- ☐ Start → Settings → Language → UTF-8 → tray 시계가 잘 보이고 type hangul.utf 도
  새 콘솔에서 정상.
- ☐ Settings 변경 후 재부팅 → 변경값 유지 확인.

### Phase 6 — Taskbar 윈도우 목록 / Alt+Tab (1일)
**목표**: 실행 중 윈도우를 taskbar 가운데 영역에 버튼으로 표시하고 Alt+Tab focus 순환을 지원한다.

- ☐ taskbar 가운데 영역(60..scrnx-50) 균등 분할, 최대 8개 버튼.
- ☐ visible 한 app sheet (system sheet 제외) 를 enumerate. button label = window title.
- ☐ click → 해당 sheet 를 top 으로, focus 이동 (`set_keywin`).
- ☐ Alt+Tab → 다음 visible app sheet, Alt+Shift+Tab → 이전.
- ☐ 버튼 hover/pressed 색상 구분, focus 중인 윈도우는 sunken 상태.

**확인할 사항**
- ☐ explorer + tetris + console 동시 실행 시 3개 버튼.
- ☐ Alt+Tab 으로 순환.
- ☐ 8개 초과 시 “…” 잘림.

### Phase 7 — UI polish / 오류 처리 / 정책 정리 (1일)
- ☐ Start Menu 항목 underline letter (e.g., `_E_xplorer`) — Phase 5 spec 에 hotkey 컬럼 추가.
- ☐ 메뉴 sheet 의 두 단계 그림자 / 1px 테두리 등 win95 톤 시각화.
- ☐ Run / About 다이얼로그 keyboard tab focus.
- ☐ 외부 click → menu close 가 같은 click 을 일반 dispatch 로 넘기는지 확인.
- ☐ MENU.CFG / SETTINGS.CFG 파싱 에러 메시지를 콘솔/디버그 윈도우에 출력.
- ☐ 색상 / 폰트 / 정렬 일관성 점검 (taskbar / menu / settings / about).
- ☐ 메뉴 깊이 / 항목 수 / 라벨 길이 한계 도달 시 일관된 자르기 / 경고.

### Phase 8 — 문서 / 회귀 검증 / 마무리 (1일)
- ☐ [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) 에 Start Menu / Run / Settings 사용법, MENU.CFG / SETTINGS.CFG 포맷 추가.
- ☐ [README.utf8.md](../README.utf8.md) 에 “시작 버튼 / 설정” 한 단락.
- ☐ [SETUP-MAC.md](../SETUP-MAC.md) 에 SYSTEM/ 디렉터리 검증 단락.
- ☐ [he2/README.md](../he2/README.md) 에 settings 앱이 신규 syscall 을 추가하지 않았음을 명시.
- ☐ 신규 문서 [_doc/menu-config.md](menu-config.md): MENU.CFG / SETTINGS.CFG 스펙.
- ☐ work5.md / work5-handoff.md 체크박스 갱신.

**확인할 사항**
- ☐ clean build / `fsck_msdos -n` 통과.
- ☐ QEMU smoke:
  - ☐ 부팅 직후 Start 버튼 visible, 시계 동작.
  - ☐ Start → Programs → 모든 1차 항목 실행.
  - ☐ Start → Settings → 4 카테고리 모두 페이지가 뜸.
  - ☐ Run… → `explorer /sub` 정상 실행.
  - ☐ Alt+Tab 으로 윈도우 순환, taskbar 버튼 클릭으로 focus 이동.
  - ☐ Settings 변경 후 재부팅 → 값 유지.
  - ☐ Shutdown / Restart 메시지 동작.
  - ☐ 기존 콘솔 명령(`langmode 0~4`, `dir`, `cd`, …) 회귀 없음.
  - ☐ explorer / tetris / winhelo3 / lines / evtest 회귀 없음.

## 4. 마일스톤 / 검증 시나리오

| 끝난 시점 | 검증 |
|---|---|
| Phase 1 | Start 버튼 hover/press, tray placeholder 가 보임. 기존 앱 회귀 없음. |
| Phase 2 | 임시 메뉴가 Start 버튼에서 펼쳐지고 키보드/마우스 둘 다 동작. |
| Phase 3 | MENU.CFG 한 항목을 바꾼 뒤 재부팅하면 Start 메뉴가 따라 바뀜. |
| Phase 4 | Start → Run → explorer / Start → About 동작. tray 시계가 분 단위로 갱신. |
| Phase 5 | Start → Settings 에서 언어 모드와 배경색 변경이 즉시/재부팅 후 반영. |
| Phase 6 | taskbar 에 실행 중 앱 목록이 표시되고 Alt+Tab 으로 순환. |
| Phase 7 | 시각적 polish 와 오류 처리, 키보드 hotkey 가 추가. |
| Phase 8 | 문서/QEMU smoke/회귀 검증 완료. |

## 5. 위험 요소 / 함정

- **시스템 sheet z-order** — taskbar / menu / cursor / debug window 가 모두
  system sheet 이다. 메뉴는 cursor 바로 아래에 있어야 마우스가 가려지지 않는다.
- **마우스 dispatch 경로** — 현재 mouse loop 는 sheet hit test 후 app event 로
  보낸다. taskbar/menu 클릭은 일반 sheet hit test 보다 **먼저** 가로채야 한다
  (window title-bar drag/close 처리 다음, app event 라우팅 직전).
- **Ctrl+Esc 단축키** — 키보드 인터럽트는 task A 의 fifo 로 들어간다.
  taskbar 가 별도 task 가 아니므로 task A 가 직접 처리해야 한다.
- **메뉴와 modal 다이얼로그 동시 노출 금지** — Run/About 이 떠 있는 동안에는
  메뉴를 열 수 없게 한다.
- **MENU.CFG 파싱 견고성** — 잘못된 줄 하나가 전체 메뉴를 망가뜨리지 않도록
  per-line tolerant 파서.
- **SETTINGS.CFG 쓰기 실패** — 디스크 가득 / handle 부족 시 사용자 통지.
- **시계 RTC 부재** — 정확한 시각 동기는 work5 범위 외. 부팅 시각을 0:00 으로
  두고 사용자 시간을 Settings 에서 set 가능하게.
- **Shutdown / Restart 부작용** — QEMU 옵션에 따라 동작이 다르다 (`-no-shutdown`).
  About 페이지에 비고로 적는다.
- **언어 모드 즉시 적용 한계** — `language.mode` 는 task 단위 변수. 새 메뉴/설정
  앱은 새 값으로 시작하지만, 이미 떠 있는 콘솔/explorer 의 langmode 는 그대로.
  spec 에 “재시작 권장” 이라 표시.
- **Alt+Tab 과 게임** — tetris 등은 키보드 입력을 직접 처리한다. Alt 자체는 키
  코드로 들어가지 않는다(현재 keyboard.c 분기 확인 필요). 충돌 가능성 있으면
  Phase 6 에서 “Alt+Tab 은 system 단축키” 로 명시 우선순위.
- **메뉴 폭 / 라벨 길이** — 한글 라벨(EUC-KR) 은 2바이트 폭이라 폭 계산이 다르다.
  Phase 7 에서 measure 함수 일관화.
- **resize 이벤트 영향** — taskbar 는 화면 크기에 의존하지만 현재 BxOS 는 화면
  해상도가 부팅 시 고정이다. work5 에서는 `binfo->scrnx/scrny` 만 본다.
- **`/SYSTEM/` 디렉터리 보존** — 사용자가 `rm /SYSTEM/MENU.CFG` 등으로 지우면
  부팅이 깨질 수 있다. 부팅 시 누락이면 빌트인 default 메뉴로 fallback.

## 6. 범위 외 (이번 작업에서 안 하는 것)

- 데스크톱 아이콘 / drag&drop / 마우스 더블클릭으로 앱 실행.
- 테마 시스템 (custom palette, theme switching).
- 다중 사용자 / 로그인 화면 / 권한.
- 시스템 트레이 plugin SDK.
- 사운드 mixer / 음량 조절 UI.
- 네트워크 / Wi-Fi 설정.
- 정확한 RTC 시계 (work5 는 부팅 시각 + uptime 만).
- 윈도우 minimize 애니메이션, fade.
- 프린터 / disk usage 카드 / 정보 dashboard.
- 다국어 라벨 자동 번역 (현재는 영어 + 일부 한글 기본 라벨).
- 빠른 검색 (start menu 검색 박스).

## 7. 예상 일정

총 **10~12 작업일** (1인 풀타임). Phase 2 (메뉴 sheet) 와 Phase 5 (Settings 스키마)
가 가장 부담이 크다. config 파일 형식과 핸들러 스키마는 Phase 0 에서 잠금이
필요하다 — 이후 phase 들이 모두 그 위에서 빌드되기 때문이다.
