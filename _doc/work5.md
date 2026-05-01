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

### Phase 0 — 요구사항 / 인터페이스 확정 (0.5일) — ☑ 완료 (2026-05-01)
- ☑ 본 문서와 [work5-handoff.md](work5-handoff.md) 를 정본으로 두고 MVP 범위 잠금.
- ☑ 다음 결정을 잠근다 (아래 표):
  - Start 버튼 hit zone, Start Menu 펼침 방향, 키보드 단축키.
  - 메뉴 / 설정 config 파일 경로(`/SYSTEM/MENU.CFG`, `/SYSTEM/SETTINGS.CFG`),
    포맷(INI-like), 핸들러 URI scheme, 빌트인 id 셋.
  - 1차 카테고리(Display/Language/Time/About) 와 각 setting key list.
  - 1차 메뉴 트리 (§3 Phase 3 예시 그대로).
- ☑ 범위 외 명시: 데스크톱 아이콘, drag&drop, theming engine, 다중 사용자,
  로그인 화면, 시스템 트레이 plugin, 사운드 mixer, 네트워크 설정 (§6).
- ☑ work2/work3/work4 와 충돌 없음 확인:
  - work2 path resolution(MAX_PATH=128B, 8.3, `/`, `.`, `..`) 그대로 사용.
    `SYSTEM` (6글자), `MENU.CFG` (4+3), `SETTINGS.CFG` (8+3), `SETTINGS.HE2` (8+3),
    `NOTEPAD.HE2` (7+3), `CALC.HE2` (4+3) 모두 8.3 적합 — 신규 이름 충돌 없음
    (현재 data.img 의 40 entries 중 동일명 없음).
  - work3 langmode 정책 그대로. work5 의 `language.mode` 는 **시스템 부팅 시
    초기 langmode 기본값** 만 결정. 콘솔의 `langmode N` 명령은 그대로 동작
    (현재 task 만 변경). Settings 앱의 변경은 즉시 SETTINGS.CFG 에 저장되며,
    시스템 적용은 다음 부팅부터 (재시작 권장 표기 — 이미 떠 있는 task 의
    langmode 는 건드리지 않음).
  - work4 syscall 번호(32~43) 보존. **work5 는 신규 사용자 syscall 0개**.
    메뉴/taskbar/시계/Run/About 은 모두 커널 내부 widget 으로 처리.
    Settings 앱은 `api_fopen`/`api_fopen_w`/`api_fread`/`api_fwrite` 등 기존 wrapper
    만 사용해 SETTINGS.CFG 를 read/write.
  - work4 `api_exec` 의 cwd 상속/subsystem 분기 정책을 메뉴 `exec:` 핸들러와
    Run… 핸들러가 그대로 재사용한다.

**Phase 0 추가 검증 노트 (2026-05-01)**
- mkfat12.py 가 root flat 만 지원 → `/SYSTEM/` 디렉터리 생성과 그 안의 파일
  install 은 **Phase 3 에서 bxos_fat.py post-processing** (mkdir + cp) 으로 처리.
  `make data-img` 의 add_custom_command 끝에 SYSTEM 항목용 명령 한 단계 추가.
  data.img root_entries=512 여유 충분(현재 40).
- 키보드 modifier 추적: 현재 `bootpack.c` 는 `key_shift` 만 추적, **Ctrl/Alt 미추적**.
  Phase 1 에서 `key_ctrl`(0x1d/0x9d), `key_alt`(0x38/0xb8) 비트마스크를 추가한다.
- 기존 Tab(scancode 0x0f) 은 char(0x09) 를 app 으로 보내면서 **동시에 window
  z-order 를 한 칸 내려 focus 를 옮긴다** ([bootpack.c:288](../harib27f/haribote/bootpack.c)).
  explorer 가 Tab 으로 tree↔list focus 를 전환하는 것과 충돌하므로 — Phase 6 에서
  이 Tab → window-cycle 동작을 **제거** 하고 **Alt+Tab** 으로 옮긴다 (회귀
  점검: explorer Tab 동작 정상화 + tetris/lines 기존 Tab 입력 영향 없음).
- 부팅 시 `init_screen8` 의 배경색 / taskbar 색은 컴파일타임 상수.
  Phase 1 에서 `g_background_color` (init=`COL8_008484`), Phase 5 의 `display.background`
  설정에서 다음 부팅에 적용.
- 시계는 RTC 가 없으므로 부팅 시각 0:00, Settings → Time 에서 사용자가 set.
  Phase 4 timer 1개 (60s 또는 1s) + Phase 5 의 `time.boot_offset_min` 으로 모델.

**Phase 0 잠금 결정 요약 (변경 시 work5.md/work5-handoff.md 동시 갱신)**

| 잠금 항목 | 값 |
|---|---|
| Start 버튼 hit zone | taskbar 좌측 (x 2..60, y `scrny-24..scrny-4`) |
| Start Menu sheet 폭 | 200px, 항목 22px, 위로 펼침 (자식 메뉴는 오른쪽으로) |
| Start 토글 단축키 | Ctrl+Esc / Esc(닫기) |
| Alt+Tab focus 순환 | system sheet(taskbar/menu/cursor/debug) 제외, visible app sheet 만. 기존 Tab→window-cycle 은 Phase 6 에서 제거 |
| Run 다이얼로그 단축키 | Ctrl+R (메뉴 열려 있지 않을 때만) |
| 메뉴 config 경로 | `/SYSTEM/MENU.CFG` (data.img) |
| 설정 영구 저장 경로 | `/SYSTEM/SETTINGS.CFG` |
| config 포맷 | INI-like, ASCII (UTF-8/EUC-KR 라벨도 허용), 줄당 1 항목, `#`/`;` 주석, `,` 구분 |
| 핸들러 URI scheme | `exec:<path>` / `builtin:<id>` / `settings:<key>` / `submenu:<section>` (정확히 4종, 알 수 없는 scheme 은 skip + warn) |
| 빌트인 id | `console`, `taskmgr`, `run`, `about`, `shutdown`, `restart`, `settings` (총 7개) |
| 1차 settings 카테고리 | `display`, `language`, `time`, `about` |
| 1차 settings key | `display.background`(enum: navy/black/gray/green), `language.mode`(enum: 0/1/2/3/4), `time.boot_offset_min`(int 0..1439, 기본 0), `time.show_seconds`(bool), `about`(action, read-only) |
| 시계 갱신 주기 | 60s timer + 메뉴/Settings 토글 시 즉시 redraw. `time.show_seconds=true` 일 때만 1s |
| 시계 표기 | `HH:MM` 또는 `HH:MM:SS` (24h ASCII 만) |
| Settings 적용 정책 | 변경 즉시 SETTINGS.CFG 저장. 시스템 변수(`g_background_color`, `g_default_langmode`, `g_clock_offset_min`, `g_clock_show_seconds`) 는 다음 **부팅 시 적용** (재시작 권장 표기). 단 Settings 앱 자체 미리보기는 즉시 |
| 메모리 한계 | 메뉴 항목 64, 카테고리당 32, 메뉴 깊이 3 |
| FAT 8.3 영향 | 신규 이름 모두 적합 (`SYSTEM`/`MENU.CFG`/`SETTINGS.CFG`/`SETTINGS.HE2`/`NOTEPAD.HE2`/`CALC.HE2`) |
| 신규 사용자 syscall | **없음**. 모든 신규 동작은 커널 내부 widget + 기존 syscall 로 해결 |
| 신규 커널 globals | `g_background_color`, `g_default_langmode`, `g_clock_offset_min`, `g_clock_show_seconds`, `g_start_menu_open`, `g_running_run_dialog`, `g_running_about_dialog` |
| 1차 신규 HE2 앱 | `SETTINGS.HE2` (Phase 5), `NOTEPAD.HE2`/`CALC.HE2` (옵션 — Phase 4 후반 또는 work6 으로 미룰 수 있음) |
| `/SYSTEM/` install 방법 | Phase 3 에서 mkfat12.py 직후 bxos_fat.py 로 mkdir + cp |
| 일정 | 10~12 작업일. 가장 부담은 Phase 2(menu sheet) + Phase 5(Settings 스키마) |

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

### Phase 1 — Taskbar / Start 버튼 / 시스템 트레이 (커널 widget) (2일) — ☑ 구현 완료, QEMU visual smoke 대기 (2026-05-01)
**목표**: 기존 `sht_back` taskbar 영역을 실제 클릭 가능한 버튼/트레이로 만든다.

- ☑ [bootpack.c](../harib27f/haribote/bootpack.c) HariMain 의 mouse loop 에 taskbar 클릭 hit-test 추가.
  - hit zone: y >= `scrny-28`. 좌측 60×20 = Start 버튼, 우측 44×20 = tray.
  - 버튼 영역에 hover/pressed 상태가 시각적으로 보이도록 `boxfill8` redraw.
- ☑ Start 버튼 라벨 그리기 (“Start” 텍스트 + win95 raised/sunken 이중선).
- ☑ 시스템 트레이 시계 placeholder (`00:00`; Phase 4 에서 실제 시간 갱신).
- ☑ Ctrl+Esc 가 들어오면 “Start 버튼 클릭” 과 동일한 이벤트로 합성.
- ☑ taskbar 의 가운데 영역(60..scrnx-50)은 **빈 영역으로 둠** — Phase 6 에서 윈도우 task list 채움.

**Phase 1 구현 노트 (2026-05-01)**
- `graphic.c` 에 `taskbar_redraw(vram, x, y, start_hover, start_pressed)` 를 추가했다.
  `init_screen8()` 는 `g_background_color` 를 읽고 desktop background + taskbar redraw 로 분리됐다.
- 신규 global: `g_background_color = COL8_008484`, `g_start_menu_open = 0`.
  `g_start_menu_open` 은 Phase 2 menu sheet 가 이어받을 토글 상태이며, Phase 1 에서는
  Start 버튼이 눌린 상태처럼 보이게 하는 데만 사용한다.
- taskbar 라벨은 초기화 순서 의존을 피하려고 langmode-aware `putfonts8_asc()` 대신
  `hankaku` 8x16 ASCII 폰트를 직접 사용한다.
- `HariMain` 은 `key_ctrl`/`key_alt` 비트마스크를 추적한다. Ctrl+Esc 는 Start 버튼
  mouse release 와 같은 `g_start_menu_open` toggle + taskbar redraw 경로로 합성된다.
- 마우스 hover/press/release 는 `sht_back` taskbar 영역에서 소비되어 app client mouse
  event 로 흘러가지 않는다. 실제 Start Menu open/close 는 Phase 2 에서 연결한다.

**확인할 사항**
- ☑ build 통과, `fsck_msdos -n` 통과.
- ☐ QEMU visual smoke: Start 버튼 hover/press 가 시각적으로 드러난다.
- ☐ QEMU regression smoke: 기존 콘솔/explorer/tetris 등 앱의 mouse 동작 회귀 없음.

### Phase 2 — Menu sheet primitive (1.5일) — ☑ 구현 완료, QEMU visual smoke 대기 (2026-05-01)
**목표**: cascading sub-menu 를 지원하는 일반 메뉴 sheet 구성요소를 추가한다.

- ☑ 신규 `harib27f/haribote/menu.c`:
  - `struct MENU_ITEM { char label[24]; int handler_id; int submenu; int flags; }`.
  - `struct KERNEL_MENU { struct SHEET *sht; struct MENU_ITEM items[..]; int selected; struct KERNEL_MENU *child; struct KERNEL_MENU *parent; }`.
  - `start_menu_toggle()`, `start_menu_close_all()`, `start_menu_handle_key()`,
    `start_menu_handle_mouse()` 를 public entry 로 제공.
- ☑ separator / disabled / submenu marker 그리기.
- ☑ keyboard navigation (↑/↓/Enter/Esc/←/→).
- ☑ mouse hover → highlight (이전 hover 항목 redraw), click → invoke.
- ☑ click-outside → root menu 까지 모두 close.
- ☑ ESC → 가장 안쪽 메뉴만 close, root 가 닫히면 focus 복귀.
- ☑ menu sheet z-order 는 cursor 바로 아래.

**Phase 2 구현 노트 (2026-05-01)**
- `struct MENU` 이름은 기존 `window.c` legacy 선언과 충돌하므로 새 primitive 는
  `struct KERNEL_MENU` 로 두었다. Phase 3 config loader 는 `MENU_ITEM` 배열만 채우면 된다.
- `SHEET_FLAG_SYSTEM_WIDGET` 를 추가해 menu sheet 가 일반 window focus/drag/resize/app-event
  대상이 되지 않게 했다.
- Start 메뉴는 임시 hard-coded tree 로 열린다: root `Programs` → child
  `Explorer`, `Console`, `Tetris`, `Task Manager`. `Settings` 등 아직 handler 가 없는
  항목은 disabled 로 표시한다.
- `Ctrl+Esc` 와 Start 버튼 click 은 모두 `start_menu_toggle()` 경로를 탄다.
  메뉴가 열린 동안 arrow/Enter/Esc 키와 menu 내부 mouse event 는 앱/콘솔로 전달되지 않는다.
  메뉴 밖 mouse down 은 메뉴를 닫고 같은 click 을 일반 dispatch 로 흘린다.

**확인할 사항**
- ☑ build 통과.
- ☐ QEMU visual smoke: 임시 fixed menu 로 Phase 1 의 Start 버튼에서 메뉴를 띄워 동작 확인.

### Phase 3 — 메뉴 config 파일 / loader (1.5일) — ☑ 구현 완료, QEMU config smoke 대기 (2026-05-01)
**목표**: 메뉴 구조를 **`/SYSTEM/MENU.CFG`** 에서 읽어 빌트인 메뉴 트리로 변환한다.

- ☑ `data.img` 빌드 시 `/SYSTEM/` 서브디렉터리 생성, `MENU.CFG` 와 (Phase 5에서) `SETTINGS.CFG` 의 default 본을 install.
  - CMake `BXOS_DATA_IMG_FILES` 또는 별도 `BXOS_SYSTEM_FILES` 리스트에 추가.
- ☑ menu.cfg 포맷 (예시):

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

- ☑ loader (`menu.c` 내부):
  - 한 줄씩 읽어 section header / key=value / `---`(separator) 파싱.
  - whitespace trim, 주석 제거, comma split.
  - 알 수 없는 핸들러는 skip.
  - 결과: `struct KERNEL_MENU` 트리.
- ☑ 부팅 시 1회 적재 (data.img mount 후, key_win 생성 전).
- ☑ Settings 는 Phase 5 앱/핸들러 연결을 위해 `builtin:settings` entry point 로 예약.

**Phase 3 구현 노트 (2026-05-01)**
- 기본 파일은 `_doc/system/menu.cfg.default`, `_doc/system/settings.cfg.default` 에 둔다.
  CMake 는 `mkfat12.py` 로 `data.img` 를 만든 직후 `bxos_fat.py mkdir/cp` 로
  `/SYSTEM/MENU.CFG`, `/SYSTEM/SETTINGS.CFG` 를 설치한다.
- `menu.c` 는 `/SYSTEM/MENU.CFG` 를 `fs_data_open_path()`/`fs_file_read()` 로 읽고
  static 4KB buffer 에서 INI-like parser 를 돌린다. 지원 범위는 section header,
  `items = a, b, ---, c`, `[item:<label>] handler = ...`, `#`/`;` 주석,
  whitespace trim 이다.
- 파싱 결과는 `struct KERNEL_MENU g_menus[KMENU_MAX_MENUS]` 와 `MENU_ITEM` 배열에
  적재된다. 잘못된 항목, 알 수 없는 handler, 비어 있는 submenu 는 skip 한다.
- `/SYSTEM/MENU.CFG` 가 없거나 root `[start]` 가 유효하지 않으면 Phase 2 의
  hard-coded fallback 메뉴로 부팅한다.
- Phase 3 에서는 handler dispatch 는 아직 하지 않는다. enabled leaf item 을 invoke 하면
  메뉴만 닫히며, 실제 `exec:`/`builtin:` 실행은 Phase 4 에서 연결한다.

**확인할 사항**
- ☑ build 통과, `fsck_msdos -n` clean, `bxos_fat.py ls /SYSTEM/` 에 `MENU.CFG` 보임.
- ☐ QEMU config smoke: MENU.CFG 의 한 줄을 일부러 망가뜨리고 부팅 → 그 항목만 빠지고 나머지 메뉴는 정상.

### Phase 4 — 핸들러 / 시계 / Run… / About (1.5일) — ☑ 구현 완료, QEMU smoke 대기 (2026-05-01)
**목표**: 메뉴 항목 클릭 시 실제 동작을 연결하고, 트레이 시계와 Run / About 다이얼로그를 추가한다.

- ☑ 핸들러 dispatcher:
  - ☑ `exec:<path>` → 기존 console/app 경로를 재사용하는 `system_start_command()` 로 실행.
  - ☑ `builtin:console` → `open_console(shtctl, memtotal)`.
  - ☑ `builtin:taskmgr` → 기존 `open_taskmgr()`.
  - ☑ `builtin:run` → Phase 4 신규 modal Run 다이얼로그.
  - ☑ `builtin:about` → Phase 4 신규 About 다이얼로그.
  - ☑ `builtin:shutdown` / `builtin:restart` → §2 의 결정대로.
  - ☑ `submenu:<section>` → `menu_open` 자식 메뉴.
  - ☑ `settings:<key>` → settings.he2 를 `<key>` 인자와 함께 실행 (Phase 5 에서).
- ☑ 시계: 60s timer. PIT timer 큐의 별도 slot 1개 예약.
  - “HH:MM” 포맷 (24h). RTC 가 없으므로 부팅 시각 + uptime, 부팅 시각은 0:00 으로 가정 (Phase 5 Time 카테고리에서 사용자 set).
  - 메뉴 토글 시점에도 즉시 redraw.
- ☑ Run… 다이얼로그:
  - 시스템 modal sheet, 폭 320 / 높이 100. 한 줄 line input + OK/Cancel.
  - OK → cmd_start 와 동일 경로. 빈 입력은 무시.
  - 다이얼로그 떠 있는 동안 키보드 focus 점유, 메뉴는 닫혀 있어야 함.
- ☑ About BxOS:
  - 시스템 modal sheet, 폭 280 / 높이 160. 텍스트 6줄: 이름, 버전, 빌드 일자,
    화면 해상도, 메모리, 현재 langmode.
  - OK 또는 Esc 로 닫음.

**Phase 4 구현 노트 (2026-05-01)**
- `menu.c` 의 leaf invoke 는 선택 항목을 복사한 뒤 menu sheet 를 닫고
  `start_menu_dispatch()` 로 넘긴다. 메뉴 닫힘과 실행 side effect 가 분리되어
  submenu/cursor z-order 회귀를 줄인다.
- `console.c` 에 `system_start_command(cmdline, memtotal)` 를 추가했다.
  `cmd_start` 와 같은 subsystem 판정을 사용해 window HE2 는 숨은 constask 로,
  console 앱/명령은 새 console sheet 로 실행한다.
- absolute path (`/EXPLORER.HE2`) 도 메뉴/Run 에서 실행되도록 `app_find()` 가
  `cons == 0` 인 경우에도 root 기준 absolute path 를 열 수 있게 했다.
- Run/About 은 `SHEET_FLAG_SYSTEM_WIDGET` system modal sheet 로 열리며, cursor 바로
  아래 z-order 로 올린다. modal 이 떠 있는 동안 Ctrl+Esc/일반 mouse routing 은
  modal 에서 소비된다.
- tray clock 은 `TIMER_CLOCK` 이벤트를 60초마다 받아 `g_clock_minutes` 를 증가시키고
  taskbar 를 다시 그린다. RTC 는 아직 쓰지 않으므로 부팅 후 `00:00` 시작이다.

**확인할 사항**
- ☑ build 통과.
- ☐ Start → Run… → `explorer` 입력 → 새 explorer 창.
- ☐ Start → About → 시스템 정보가 정확.
- ☐ Start → Programs → Games → Tetris → 정상 실행 (콘솔 없이).
- ☐ Start → Programs → Console → 새 콘솔 창.
- ☐ tray 시계가 분 단위로 갱신.

### Phase 5 — Settings 앱 (선언적 스키마 기반) (2일) — ☑ 구현 완료, QEMU smoke 대기 (2026-05-01)
**목표**: 모던 Settings 앱처럼 선언으로부터 자동 생성되는 설정 UI 를 만든다.

- ☑ 새 HE2 window 앱 `harib27f/settings/settings.c` (`SETTINGS.HE2`).
- ☑ 스키마 (settings.c 내 `static const`):

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
- ☑ 좌측 카테고리 리스트, 우측 페이지. 페이지는 해당 카테고리 entry 만 그린다.
- ☑ 위젯 자동 생성:
  - ☑ ENUM → 라디오 + 라벨.
  - ☑ BOOL → 체크박스.
  - ☑ INT → `[ - ] N [ + ]` 버튼.
  - ☐ STR → 한 줄 input (현재 1차 schema 에 STR 항목 없음).
  - ☑ ACTION → 버튼 (현재는 About 안내).
- ☑ 값 변경 즉시:
  - ☑ 시스템 변수 갱신 (`language.mode` → kernel 의 current task->langmode 정책 변경 메시지 또는
    syscall, 시각적 효과만 반영) — 1차는 “재시작 시 적용” 으로 명시 가능한 항목과
    “즉시 적용” 항목을 spec 에 표기.
  - ☑ `/SYSTEM/SETTINGS.CFG` 재기록 (`api_fopen_w` + `api_fwrite`).
  - ☑ 실패 시 status “save failed”.
- ☑ 부팅 시 SETTINGS.CFG 를 읽어 spec 의 default 를 덮어쓴 뒤 시스템에 적용.
  - ☑ `display.background` → `init_screen8` 에 사용할 색 변수.
  - ☑ `language.mode` → 부팅 직후 task A 와 새 console task 의 langmode.
  - ☑ `time.show_seconds` → tray 시계 포맷.
- ☑ 카테고리 트리는 현재 4개 → 추가 시 `g_settings[]` 와 `g_categories[]` 에 추가.

**Phase 5 구현 노트 (2026-05-01)**
- `harib27f/settings/settings.c` 를 HE2 window app 으로 추가하고, CMake 의
  `BXOS_HE2_APPS_WINDOW` 에 `settings` 를 등록했다. 결과물은 data.img 루트의
  `SETTINGS.HE2` 로 설치된다.
- Settings 앱은 `/SYSTEM/SETTINGS.CFG` 를 읽고, 변경 즉시 전체 설정 파일을
  `api_fopen_w` + `api_fwrite` 로 재작성한다.
- 현재 schema 는 Display / Language / Time / About 4개 카테고리다.
  Display/Language 는 enum, Time 의 offset 은 int stepper, show_seconds 는 bool,
  About 은 안내 action 이다.
- 커널은 data drive mount 직후 `/SYSTEM/SETTINGS.CFG` 를 읽는다.
  `display.background`, `language.mode`, `time.boot_offset_min`,
  `time.show_seconds` 를 부팅 기본값으로 적용한다.
- `time.show_seconds=true` 일 때 tray clock 은 `HH:MM:SS` 로 표시하고 1초 timer 로,
  기본값에서는 `HH:MM` 과 60초 timer 로 동작한다.

**확인할 사항**
- ☑ build 통과, `bxos_fat.py ls /` 에 `SETTINGS.HE2` 보임.
- ☐ Start → Settings → Language → UTF-8 → tray 시계가 잘 보이고 type hangul.utf 도
  새 콘솔에서 정상.
- ☐ Settings 변경 후 재부팅 → 변경값 유지 확인.

### Phase 6 — Taskbar 윈도우 목록 / Alt+Tab (1일) — ☑ 구현 완료, QEMU smoke 대기 (2026-05-01)
**목표**: 실행 중 윈도우를 taskbar 가운데 영역에 버튼으로 표시하고 Alt+Tab focus 순환을 지원한다.

- ☑ taskbar 가운데 영역(`TASKBAR_WIN_X0=64 .. tray_left - 5`) 균등 분할, 최대 8개 버튼.
- ☑ visible 한 app sheet (system sheet 제외) 를 enumerate. button label = window title (없으면 "Console" / task->name / "Debug").
- ☑ click → 해당 sheet 를 top 으로, focus 이동 (`set_keywin`).
- ☑ Alt+Tab → 다음 visible app sheet, Alt+Shift+Tab → 이전.
- ☑ 버튼 hover/pressed 색상 구분, focus 중인 윈도우는 sunken 상태.
- ☑ 기존 Tab → window z-order swap ([bootpack.c](../harib27f/haribote/bootpack.c) 의 line 746) 제거. Tab(0x0f) 은 `keytable0`/`keytable1` 의 0x09 char 로 변환되어 focused app 에 그대로 전달된다 (explorer tree↔list focus 정상화).

**Phase 6 구현 노트 (2026-05-01)**
- 부팅 시 자동 실행되던 **console / debug 두 창을 모두 제거**했다. 부팅 직후
  화면에는 desktop + taskbar (Start/시계) 만 보이고, 사용자는 Start Menu /
  Run / Shift+F2 (콘솔 단축키) / 메뉴 → Debug 항목으로 직접 띄운다.
  `key_win` 은 0 으로 시작하며, `keywin_off`/`keywin_on` 은 NULL 입력에
  null-safe (no-op) 하도록 보강했다.
- Debug window 는 부팅 시점에는 `dbg_init` 으로 sheet/scrollwin alloc 만 하고
  hidden 상태로 둔다. Start → Programs → Debug (`builtin:debug`) 또는 콘솔의
  taskmgr 출력 등으로 처음 호출되는 시점에 `dbg_open()` 가 화면에 띄우고
  focus 로 끌어올린다. taskbar 윈도우 목록에는 SCROLLWIN 플래그로 자동 등장한다.
- 새 close 경로는 `find_topmost_app_sheet(shtctl)` 로 next focus 를 결정한다
  — sht_mouse / sht_back / system widget 을 모두 건너뛰고 가장 위에 있는
  실제 app sheet 을 고르거나, 후보가 없으면 0 반환.
- 신규 globals (bootpack.c): `g_sht_back`, `g_buf_back`, 그리고 static 한
  `g_taskbar_btns[8]` / `g_taskbar_btn_count` / `g_taskbar_btn_hover` /
  `g_taskbar_btn_pressed`. layout 계산은 `taskbar_winlist_layout()` 가 한다.
- `taskbar_full_redraw(start_hover, start_pressed)` 가 background+Start+tray
  drawing(`taskbar_redraw`) 과 윈도우 목록 버튼을 한 번에 그리고 `sht_back` 을
  refresh 한다. 기존 `taskbar_redraw + sheet_refresh` 호출 위치는 모두 이 함수로 교체.
- `taskbar_mark_dirty()` 는 다음 fifo 이벤트 처리 직후 한 번 redraw 가 필요함을
  표시한다. window close/ open / focus 변경 / start_menu dispatch / 새 sheet
  open 직후 호출. mouse loop 의 마지막에 `g_taskbar_dirty` 가 set 이면
  `taskbar_full_redraw` 후 clear.
- 윈도우 목록 enumeration: `sheets0[]` 인덱스 순서로 안정적. 포함 조건은
  `SHEET_USE && height>=1 && !SYSTEM_WIDGET && !sht_mouse && !sht_back &&
   (APP_WIN || HAS_CURSOR || SCROLLWIN || task != 0)`. focus 표시는 z-order
  최상단 eligible sheet 가 sunken 상태로 표시된다.
- Alt+Tab/Alt+Shift+Tab: keyboard handler 의 키 분기에서 `i == 256+0x0f &&
  key_alt`. visible app sheet 들을 z-order 상위→하위 순서로 list 화한 뒤
  현재 focus(=list[0]) 의 다음/이전 항목을 raise + system_request_keywin.
  start menu / system modal 가 떠 있으면 무시.
- Tab char 전달: `keytable0[0x0f] = 0x09`, `keytable1[0x0f] = 0x09` 추가. modal
  handler 가 Tab 을 현재 무시하므로 Run dialog 입력에는 영향 없음 (32~126 필터로 drop).
  이전의 Tab → window z-order swap 코드 (HariMain key handler) 는 제거했다.

**확인할 사항**
- ☑ build 통과, `fsck_msdos -n` clean.
- ☐ QEMU visual smoke: explorer + tetris + console 동시 실행 시 taskbar 에 3개 버튼.
- ☐ Alt+Tab / Alt+Shift+Tab 순환.
- ☐ taskbar 버튼 click → focus 이동 + sheet top.
- ☐ 8개 초과 시 button width 가 min(32) 로 clamp 후 잘림.
- ☐ explorer 의 Tab(tree↔list) focus 회귀 정상화 확인.
- ☐ tetris/lines 의 입력 흐름 회귀 없음.

### Phase 7 — UI polish / 오류 처리 / 정책 정리 (1일) — ☑ 구현 완료, QEMU smoke 대기 (2026-05-01)
- ☑ Start Menu 항목 hotkey underline. `[item:<label>]` 섹션에 `hotkey = X` 키 추가
  → 라벨 안의 첫 매칭 글자를 underline 으로 표시하고, 메뉴가 열린 동안 그 글자
  키로 항목 invoke. fallback 메뉴와 `MENU.CFG.default` 둘 다 hotkey 부여됨.
- ☑ 메뉴가 열린 동안 키보드 leak 차단 — navigation/hotkey 외 모든 char 도 메뉴
  레이어가 consume (이전엔 underlying app 으로 leak 가능).
- ☐ 메뉴 sheet drop shadow / 두 단계 그림자 — 보류. 메뉴 sheet 를 +2px 확장하고
  col_inv 로 코너만 투명 처리해야 해서 비용 대비 효과가 작음. work6 polish.
- ☑ Run 다이얼로그 keyboard Tab focus — Tab 으로 input ↔ OK ↔ Cancel 순환,
  포커스된 버튼은 1px 점선 테두리, Enter 는 포커스 위치(input/OK=submit, Cancel=close),
  Space 는 포커스된 버튼 활성화. About 은 OK 한 개라 별도 포커스 없음 (그대로).
- ☑ 외부 click → menu close 동작 확인. `start_menu_handle_mouse` 가 menu hit 실패
  시 close 만 하고 return 0 하므로, HariMain 의 일반 dispatch 가 같은 click 을
  이어받는다 (Phase 2 의 결정 그대로). 별도 변경 없음.
- ☑ MENU.CFG / SETTINGS.CFG 파싱 에러 보고 — debug window 의 `dbg_putstr0` 로
  `[menu] bad handler: …`, `[settings] key = value: …` 형태로 누적. Settings 는
  알 수 없는 키, 잘못된 enum/int/bool 값을 모두 신고.
- ☑ 색상 / 폰트 / 정렬 일관성: taskbar/menu/about/run 모두 `COL8_C6C6C6` 배경 +
  `FFFFFF`/`848484`/`000000` win95 라이즈/언더 톤 통일. ASCII 라벨은 hankaku 직접
  사용으로 langmode 영향 없음.
- ☑ 메뉴 라벨 길이 한계 — 폭에 맞춰 자르고 마지막 글자를 `.` 로 대체
  (`menu_label_fit`). 너비 = `menu->w - 16(좌pad) - 16(arrow)` 기준.

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
