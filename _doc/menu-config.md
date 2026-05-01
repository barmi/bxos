# MENU.CFG / SETTINGS.CFG 스펙 (work5)

데이터 디스크 (`data.img`) 의 `/SYSTEM/` 디렉터리 안에 두는 두 개의
**선언적 설정 파일**입니다. 부팅 시 커널이 한 번 읽어 Start Menu 트리와
시스템 설정을 구성합니다. default 본은 `_doc/system/menu.cfg.default`,
`_doc/system/settings.cfg.default` 에 있고, CMake 가 `data-img` 빌드 직후
`bxos_fat.py mkdir/cp` 로 `/SYSTEM/` 에 install 합니다.

| 파일 | 용도 |
|---|---|
| `/SYSTEM/MENU.CFG` | Start Menu 트리 / 항목 / 핸들러 / hotkey 선언 |
| `/SYSTEM/SETTINGS.CFG` | 사용자 설정 영구 저장 본 (Settings 앱이 자동 갱신) |

두 파일 모두 ASCII INI-like 포맷이고, UTF-8/EUC-KR 라벨도 허용합니다 (출력은
현재 langmode 에 따라 그려집니다).

## 1. 공통 파싱 규칙

* 한 줄에 한 항목. CR/LF 모두 줄바꿈으로 인식.
* 줄 시작이 `#` 또는 `;` 이면 주석. 줄 안에서 `#`/`;` 가 처음 등장하면 그 뒤는 주석.
* 양 끝의 공백/탭/`\r`/`\n` 은 trim.
* 빈 줄은 skip.
* `key = value` 의 양 옆 공백은 trim.
* 알 수 없는 line/key 는 무시 — 단 MENU.CFG 의 잘못된 `handler =` 와
  SETTINGS.CFG 의 알 수 없는 key/value 는 **debug 창**에 경고로 누적되며,
  `Start → Programs → Debug` 로 확인 가능합니다.

파싱 한계 (커널 내부 정적 배열):

| 한계 | 값 |
|---|---|
| MENU.CFG 파일 크기 | 4 KB |
| SETTINGS.CFG 파일 크기 | 512 B |
| 메뉴 섹션 수 | 8 |
| 한 메뉴 안 항목 수 | 64 |
| 항목 라벨 길이 | 23 chars |
| `[item:*]` 정의 수 | 64 |
| `submenu` 깊이 | 3 |

라벨이 폭(`menu->w - 32 px`)을 넘어가면 메뉴 그릴 때 마지막 글자를 `.` 로
대체해 자릅니다.

## 2. `/SYSTEM/MENU.CFG`

### 2.1 섹션 종류

* `[start]` — root menu. 반드시 있어야 부팅 시 메뉴가 동작합니다.
  없거나 비어 있으면 커널이 hard-coded fallback 메뉴로 부팅합니다.
* `[start/<sub>]`, `[start/<sub>/<sub2>]` — submenu. depth 3 까지.
* `[item:<라벨>]` — 항목 정의. `items = ...` 에 적힌 같은 라벨과 매칭됩니다.

각 섹션 안에서 인식되는 키:

| 섹션 종류 | 키 | 의미 |
|---|---|---|
| `[start]` 계열 | `items` | 표시 순서대로 라벨 (또는 `---`) 을 `,` 로 구분 |
| `[item:*]` | `handler` | 핸들러 URI (아래 §2.3) |
| `[item:*]` | `hotkey` | 한 글자 (대소문자 무시). 라벨 안 첫 매칭 글자 underline |

`[item:*]` 의 다른 키 (예: `shortcut`) 는 현재 무시됩니다 (문서화/주석용).

### 2.2 separator

`items = …` 안의 `---` 는 separator (가로선) 로 그려집니다. `[item:---]`
같은 정의가 따로 있으면 무시됩니다.

### 2.3 핸들러 URI scheme

| URI | 동작 |
|---|---|
| `exec:<절대경로>` | `system_start_command()` 로 실행. cwd 는 `<절대경로>` 의 부모. |
| `builtin:<id>` | 아래 빌트인 표 |
| `settings:<key>` | `SETTINGS.HE2 <key>` 인자로 실행 (Settings 앱이 해당 카테고리 페이지로 이동) |
| `submenu:<섹션>` | 해당 섹션을 child menu 로 펼침 |

**빌트인 id** (정확히 8개, 그 외는 알 수 없음으로 skip):

| id | 동작 |
|---|---|
| `console` | 새 콘솔 창 |
| `taskmgr` | Task Manager 창 |
| `run` | Run 다이얼로그 |
| `about` | About BxOS 다이얼로그 |
| `settings` | Settings 앱 (`/SETTINGS.HE2`) |
| `debug` | hidden 이던 debug window 를 띄우고 focus |
| `shutdown` | "It is now safe to power off." 표시 후 `cli; hlt` |
| `restart` | 키보드 컨트롤러 reset (`out 0x64, 0xfe`) |

알 수 없는 scheme 또는 알 수 없는 builtin id 는 그 항목만 skip 되고
debug 창에 `[menu] bad handler: <라벨> = <값>` 로그를 남깁니다.

### 2.4 예시

```ini
# /SYSTEM/MENU.CFG

[start]
items = Programs, Settings, ---, Run..., About BxOS, ---, Restart, Shutdown

[start/Programs]
items = Explorer, Console, Notepad, Calc, Games, ---, Task Manager, Debug

[start/Programs/Games]
items = Tetris

[item:Programs]
handler = submenu:start/Programs
hotkey  = P

[item:Settings]
handler = builtin:settings
hotkey  = S

[item:Run...]
handler = builtin:run
hotkey  = R

[item:About BxOS]
handler = builtin:about
hotkey  = A

[item:Restart]
handler = builtin:restart
hotkey  = R

[item:Shutdown]
handler = builtin:shutdown
hotkey  = U

[item:Explorer]
handler = exec:/EXPLORER.HE2
hotkey  = E

[item:Console]
handler = builtin:console
hotkey  = C

[item:Tetris]
handler = exec:/TETRIS.HE2
hotkey  = T

[item:Task Manager]
handler = builtin:taskmgr
hotkey  = T

[item:Debug]
handler = builtin:debug
hotkey  = D

[item:Games]
handler = submenu:start/Programs/Games
hotkey  = G
```

## 3. `/SYSTEM/SETTINGS.CFG`

`Settings.he2` 가 사용자 변경 시 전체 파일을 다시 기록합니다. 부팅 시 커널이
한 번 읽어 시스템 변수에 적용합니다 (즉시 적용 — 단 이미 떠 있는 task 의
langmode 같은 것은 다음 부팅부터).

### 3.1 인식되는 키

| 키 | 타입 | 허용값 | 적용처 |
|---|---|---|---|
| `display.background` | enum | `navy` / `black` / `gray` / `green` | `g_background_color` (다음 부팅 시 desktop 색) |
| `language.mode` | enum | `0` / `1` / `2` / `3` / `4` | `g_default_langmode` (부팅 시 task A 와 신규 console) |
| `time.boot_offset_min` | int | 0..1439 | `g_clock_seconds` 초기값 (분 단위) |
| `time.show_seconds` | bool | `true`/`1` / `false`/`0` | `g_clock_show_seconds` (시계 표시 / timer 주기) |

알 수 없는 키, 잘못된 enum/int/bool 값은 default 로 fallback 후 debug 창에
`[settings] <key> = <value>: <reason>` 로그.

### 3.2 예시

```ini
# /SYSTEM/SETTINGS.CFG -- modified by Settings.he2 on save.
display.background = navy
language.mode = 0
time.boot_offset_min = 0
time.show_seconds = false
```

## 4. 호스트 검증

```bash
# /SYSTEM/ 안 파일 목록
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/SYSTEM

# 설치된 본 vs default 비교
python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/SYSTEM/MENU.CFG /tmp/m.cfg
diff _doc/system/menu.cfg.default /tmp/m.cfg

python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/SYSTEM/SETTINGS.CFG /tmp/s.cfg
diff _doc/system/settings.cfg.default /tmp/s.cfg
```

게스트 안에서는 콘솔의 `type /SYSTEM/MENU.CFG` 로 확인할 수 있습니다.

## 5. 신규 syscall

**없음**. Start Menu / taskbar / 시계 / Run / About 은 모두 커널 내부 widget
이고, `Settings.he2` 는 기존 file syscall (28~30, 32~39) 만 사용합니다. 자세한
syscall 표는 [`he2/docs/HE2-FORMAT.md`](../he2/docs/HE2-FORMAT.md#syscall-디스패치-edx).

## 6. 누락 / 망가졌을 때

* `/SYSTEM/MENU.CFG` 가 없으면 커널은 hard-coded fallback 메뉴 (Programs/Settings/Run/About/...) 로 부팅합니다. 이 fallback 의 내용은 [`harib27f/haribote/menu.c`](../harib27f/haribote/menu.c) 의 `start_menu_init()`.
* `/SYSTEM/SETTINGS.CFG` 가 없거나 비어 있으면 `display.background=navy`, `language.mode=0`, `time.boot_offset_min=0`, `time.show_seconds=false` 가 default.
* 잘못된 line 한 개는 그 항목만 skip — 나머지 메뉴/설정은 정상 동작합니다.
