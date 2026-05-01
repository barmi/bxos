# BxOS 콘솔 명령어 / 포함 앱 빠른 참조

이 문서는 `./run-qemu.sh` 로 부팅한 뒤 콘솔 창에서 입력할 수 있는 명령을 정리합니다. 기본 구성은 `build/cmake/haribote.img` 를 FDD(`A:`)로 부팅하고 `build/cmake/data.img` 를 HDD(`C:`) 데이터 디스크로 붙입니다.

work5 부터는 부팅 직후 데스크톱이 비어 있고 화면 하단에 **Start 버튼 + 시스템 트레이 시계** 만 보입니다. 콘솔/탐색기/설정 등은 좌측 Start 버튼(또는 `Ctrl+Esc`)을 눌러 열거나, **Start → Run...** (또는 `Ctrl+R`) 으로 명령을 실행할 수 있습니다. 자세한 조작은 아래 ["Start Menu / Run / Settings"](#start-menu--run--settings-work5) 단락 참고.

명령 이름은 기본적으로 소문자로 입력하는 것을 권장합니다. 앱 파일은 FAT 8.3 이름으로 저장되어 있으며, `.he2` 확장자는 생략할 수 있습니다. 예를 들어 `winhelo` 와 `winhelo.he2` 는 같은 앱을 실행합니다.

파일 경로는 데이터 디스크(`C:`) 안에서 `/` 로 구분합니다. 절대 경로(`/sub/a.txt`)와 현재 작업 디렉터리 기준 상대 경로(`sub/a.txt`, `../top.txt`)를 모두 사용할 수 있습니다. 파일/디렉터리 이름은 FAT 8.3 형식으로 저장되며, 긴 이름은 호스트 도구/게스트 모두 단순 절단 + 대문자 변환 규칙을 따릅니다.

## 기본 명령어

| 명령 | 설명 |
|---|---|
| `mem` | 전체 메모리와 사용 가능한 메모리를 표시합니다. |
| `cls` | 현재 콘솔 창의 표시 영역을 지웁니다. |
| `dir [경로]` | 경로가 없으면 현재 디렉터리, 경로가 있으면 해당 디렉터리나 파일 정보를 표시합니다. |
| `cd <경로>` | 현재 작업 디렉터리를 변경합니다. `/`, `.`, `..` 를 사용할 수 있습니다. |
| `pwd` | 현재 작업 디렉터리를 표시합니다. |
| `mkdir <경로>` | 빈 디렉터리를 만듭니다. 부모 디렉터리는 이미 있어야 합니다 (`mkdir -p` 미지원). |
| `rmdir <경로>` | 빈 디렉터리를 삭제합니다. `.`/`..` 외 파일이나 하위 디렉터리가 있으면 실패합니다. |
| `task` | 실행 중인 태스크 목록을 Debug Window에 표시합니다. |
| `taskmgr` | 태스크 매니저 창을 엽니다. (Start → Programs → Task Manager 와 동일.) |
| `disk` | ATA 디스크 인식 정보와 데이터 디스크 부트 섹터를 표시합니다. |
| `touch <경로>` | 빈 파일을 만듭니다. 이미 있으면 그대로 둡니다. |
| `rm <경로>` | 파일을 삭제하고 FAT 클러스터를 해제합니다. 디렉터리는 `rmdir` 로 지웁니다. |
| `cp <원본> <대상>` | 파일을 raw bytes 그대로 복사합니다. 예: `cp /tetris.he2 tetris2.he2` |
| `mv <원본> <대상>` | 파일을 이동하거나 이름을 바꿉니다. 내부적으로 복사 후 원본을 삭제합니다. |
| `echo <문자열> > <경로>` | 문자열과 줄바꿈을 파일에 저장합니다. 기존 파일은 덮어씁니다. |
| `mkfile <경로> <바이트>` | A-Z 반복 패턴으로 지정한 크기의 파일을 만듭니다. 예: `mkfile /big.bin 102400` |
| `exit` | 현재 콘솔 태스크를 종료합니다. 기본 콘솔 창에서 쓰면 창이 닫힐 수 있습니다. |
| `start <명령>` | 새 콘솔 창을 열고 그 안에서 `<명령>`을 실행합니다. 예: `start dir`, `start winhelo` |
| `ncst <명령>` | 콘솔 창 없이 새 콘솔 태스크를 만들고 `<명령>`을 실행합니다. 창을 직접 여는 앱 실행에 유용합니다. 예: `ncst winhelo` |
| `langmode 0` | ASCII 모드로 전환합니다. |
| `langmode 1` | 일본어 EUC 계열 모드로 전환합니다. |
| `langmode 2` | 일본어 Shift-JIS 계열 모드로 전환합니다. |
| `langmode 3` | 한국어 EUC-KR 모드로 전환합니다. |
| `langmode 4` | 한국어 UTF-8 모드로 전환합니다. |

### 경로 예시

```text
mkdir /sub
mkdir /sub/inner
cd /sub
pwd
touch a.txt
echo hello > inner/msg.txt
dir /sub
cp inner/msg.txt ../msg.txt
rm ../msg.txt
rm inner/msg.txt
rmdir /sub/inner
```

`start <명령>` 또는 `ncst <명령>` 으로 실행한 새 콘솔/앱은 실행 시점의 현재 작업 디렉터리를 상속합니다.

## 앱 실행 방법

콘솔에서 파일명만 입력하면 됩니다 (확장자 `.he2` 는 생략 가능).

```text
a
hello3
winhelo
tetris
type euc.txt
chklang
type hangul.utf
```

파일 목록은 `dir` 로 확인할 수 있습니다. 앱 실행 시 `Bad command or file name.` 이 나오면 파일명이 틀렸거나 이미지에 해당 파일이 없는 것입니다.

## 포함 앱 (HE2 28개)

| 명령 | 설명 |
|---|---|
| `a` | 콘솔에 `A`를 출력합니다. |
| `hello3`, `hello4` | 콘솔에 hello 메시지를 출력하는 예제입니다. |
| `winhelo`, `winhelo2`, `winhelo3` | 작은 창을 열어 hello 메시지를 표시합니다. Enter로 종료합니다. |
| `star1`, `stars`, `stars2` | 점/별을 창에 그리는 그래픽 예제입니다. Enter로 종료합니다. |
| `lines` | 여러 색상의 선을 그립니다. Enter로 종료합니다. |
| `walk` | `*` 문자를 커서키로 움직이는 예제입니다. Enter로 종료합니다. |
| `noodle` | 타이머 카운트다운 예제입니다. |
| `beepdown` | 비프음 주파수를 낮추는 예제입니다. 키를 누르면 종료합니다. |
| `color`, `color2` | 색상 팔레트/그라데이션 표시 예제입니다. 키를 누르면 종료합니다. |
| `sosu`, `sosu2`, `sosu3` | 소수(prime number)를 계산해 콘솔에 출력합니다. |
| `tetris` | 테트리스 게임. 커서키 / Space / Enter 사용. |
| `type <파일>` | 텍스트 파일 내용을 콘솔에 출력합니다. 예: `type euc.txt` |
| `touch.he2 <경로>` | 사용자 API 경유로 빈 파일을 만듭니다. 기존 파일은 보존합니다. |
| `echo.he2 <문자열> > <경로>` | 사용자 API 경유로 문자열과 줄바꿈을 파일에 저장합니다. 내장 `echo`와 구분하려면 `.he2`를 붙여 실행합니다. |
| `fdel <경로>` | 사용자 API 경유로 파일을 삭제합니다. |
| `pwd.he2` | 사용자 API 경유로 앱이 상속받은 현재 작업 디렉터리를 출력합니다. |
| `chklang` | 현재 언어 모드(ASCII, 일본어, 한국어 등)를 출력합니다. |
| `khello` | UTF-8 으로 인코딩된 정적 한글 인사말을 표시합니다 (사용 시 langmode 4 권장). |
| `lsdir [경로]` | 사용자 API(`api_opendir`/`readdir`/`closedir`) 경유로 디렉터리 항목과 크기/`<DIR>` 표시를 콘솔에 출력합니다. |
| `evtest` | 창 안 마우스 클릭/이동/리사이즈 이벤트를 점/박스/프레임으로 시각화하는 검증 앱입니다. 우하단 모서리 드래그로 창 크기를 바꿀 수 있습니다. |
| `explorer [경로]` | 2-pane 파일 탐색기. 왼쪽 디렉터리 트리, 오른쪽 파일 목록, 상단 toolbar(`<-`/`..`/`R`/`N`/`D`/`M`), 하단 status. 키보드/마우스/창 리사이즈 모두 지원합니다. 자세한 조작은 아래 ["explorer 사용법"](#explorer-사용법) 참고. |

> 옛 HRB 앱(`calc`, `tview`, `mmlplay`, `gview`, `invader`, `bball`, `notrec`, `iroha`, `chklang` 등) 은 현재 데이터 디스크에 포함되지 않습니다. work1 작업으로 이미지에 들어가는 앱이 HE2 포맷으로 한정됐고, 이 앱들은 아직 HE2 로 마이그레이션되지 않았습니다.

### explorer 사용법

```text
explorer            # 현재 cwd 에서 시작
explorer /sub       # /sub 까지 트리를 자동으로 펼치고 시작
start explorer      # 새 콘솔에서 실행 (콘솔이 종료를 기다리지 않음)
```

기본 창 크기는 420×280, 최소 320×200. 우하단/오른쪽/아래 모서리 드래그로 창 크기를 바꿀 수 있습니다.

**키보드**

| 키 | 동작 |
|---|---|
| ↑ / ↓ | 트리 또는 파일 목록 선택 이동 |
| ← / Backspace | 트리: collapse 또는 부모 노드. 목록: 부모 디렉터리. preview: 목록 복귀. |
| → | 트리: expand. 목록: Enter 와 동일 (디렉터리 진입 / `.HE2` 실행 / 그 외 preview). |
| Enter | 디렉터리 진입, `.HE2` 실행, 또는 텍스트/hex preview. preview: 목록 복귀. |
| Tab | 트리 ↔ 파일 목록 focus 전환 |
| `r` | 현재 디렉터리 reload |
| `n` | 새 디렉터리 만들기 (status 입력 모드) |
| `m` | rename / 같은 부모 안에서 이름 바꾸기 |
| `d` | 선택 항목 삭제 (`y` 로 confirm) |
| ESC / `q` | preview 종료 또는 앱 종료 |

**마우스**

| 동작 | 결과 |
|---|---|
| tree/list row click | 해당 row 선택 + focus 이동 |
| 같은 row 재click | open (트리: expand toggle, 목록: 디렉터리 진입 / `.HE2` 실행 / preview) |
| splitter drag | 트리/목록 폭 비율 조정 (창 resize 후에도 비율 유지) |
| splitter hover | 좌우 resize 커서로 변경 |
| scrollbar thumb drag / track click / arrow click | 트리/목록/preview 스크롤 |
| 우하단/오른쪽/아래 edge drag | 창 크기 변경 (드래그 중 실시간 layout 반영) |
| toolbar `<-`/`..`/`R`/`N`/`D`/`M` 클릭 | 부모 / 부모 / refresh / new / delete / rename |

**파일 동작**

- 디렉터리는 진한 파란색, `.HE2` 실행 파일은 진한 녹색, 일반 파일은 검정색으로 구분합니다.
- `.HE2` 를 Enter / double-click 으로 실행하면 새 콘솔 task 가 만들어지며, 실행 앱의 cwd 는 해당 `.HE2` 의 부모 디렉터리로 설정됩니다.
- 텍스트가 아닌 파일은 size/cluster + 첫 N bytes hex view 로 표시합니다.
- 파일관리(`mkdir`/`rmdir`/`rename`)는 콘솔의 동명 명령과 같은 FAT 16 8.3 규칙을 공유합니다. cross-parent move 는 지원하지 않습니다 (`api_rename` 정책).
- 빈 디렉터리만 삭제할 수 있습니다. recursive delete 는 미지원.

자세한 syscall API 와 ABI 는 [`he2/docs/HE2-FORMAT.md`](he2/docs/HE2-FORMAT.md) 참고.

## 포함 데이터 파일

| 파일 | 용도 |
|---|---|
| `euc.txt` | 일본어 EUC 텍스트 파일입니다. |
| `hangul.euc` | EUC-KR 한글 텍스트 파일입니다. `langmode 3` 후 `type hangul.euc` 로 확인합니다. |
| `hangul.utf` | UTF-8 한글 텍스트 파일입니다. `langmode 4` 후 `type hangul.utf` 로 확인합니다. |
| `kirakira.mml`, `daigo.mml`, `daiku.mml` | MML 음악 데이터 (현재 재생 앱은 미포함). |
| `fujisan.jpg`, `night.bmp` | 이미지 파일 (현재 뷰어 앱은 미포함). |
| `ipl09.nas`, `make.bat` | 빌드 시점 자료 — `cp` / `type` 동작 검증용으로 들어 있습니다. |

부팅 FDD(`A:`) 에는 `HARIBOTE.SYS` + `NIHONGO.FNT` 만 들어 있고, 콘솔/`dir` 등은 모두 데이터 HDD(`C:`) 만 봅니다. 자세한 디스크 구조는 [`_doc/storage.md`](_doc/storage.md) 참고.

## 입력 팁

창을 여는 앱은 실행 후 Enter를 눌러 닫는 경우가 많습니다. 일부 앱은 커서키, Space, 숫자 키를 사용합니다.

기본 콘솔에서 앱을 실행하면 콘솔이 앱 종료를 기다릴 수 있습니다. 앱을 별도 콘솔에서 실행하려면 `start <앱명>`, 창만 필요한 앱을 조용히 실행하려면 `ncst <앱명>`을 사용해 보세요.

## Start Menu / Run / Settings (work5)

work5 부터 화면 하단에 win95 스타일 **taskbar** 가 추가됩니다. 좌측에 Start 버튼, 중앙에 실행 중 윈도우 목록 버튼, 우측에 시계가 있습니다.

### Start 버튼

* `Ctrl+Esc` 또는 좌측 `Start` 버튼 클릭으로 토글.
* `↑` / `↓` 로 항목 이동, `→` / `Enter` 로 submenu 진입, `←` / `Esc` 로 한 단계 닫기.
* 메뉴 항목 라벨에 **밑줄** 이 표시된 글자는 hotkey — 그 글자를 누르면 즉시 실행 (Win95 스타일).
* 메뉴 밖을 클릭하면 닫힙니다.

기본 트리 (Phase 8 시점):

```
Programs ▶  Explorer (E)
            Console (C)
            Notepad (N) — 미구현 (Calc 와 함께 work6)
            Calc (L) — 미구현
            Games ▶  Tetris (T)
            ---
            Task Manager (T)
            Debug (D)
Settings (S)
---
Run... (R)            ← Ctrl+R
About BxOS (A)
---
Restart (R)
Shutdown (U)
```

### Run 다이얼로그

* `Ctrl+R` 또는 `Start → Run…` 으로 모달 다이얼로그.
* 입력 → `Enter` 또는 `OK` 로 실행. cwd 는 부팅 시점의 루트 (`/`).
* `Tab` 으로 입력란 ↔ `OK` ↔ `Cancel` focus 순환. 포커스된 버튼은 1px 점선 테두리.
* `Esc` 로 닫기. `Cancel` focus 시 `Enter` 도 닫기.

```text
Open: /EXPLORER.HE2          ← 직접 경로
Open: explorer /sub          ← 콘솔 이름 + 인수
```

### Settings 앱

* `Start → Settings` 또는 콘솔에서 `/SETTINGS.HE2`.
* 좌측 카테고리 (`Display` / `Language` / `Time` / `About`), 우측 자동 생성 페이지.
* 변경 즉시 `/SYSTEM/SETTINGS.CFG` 에 저장. 시스템 변수는 다음 부팅부터 적용 (Settings 앱 미리보기는 즉시).

| 카테고리 | 키 | 위젯 / 값 |
|---|---|---|
| Display | `display.background` | enum: `navy` / `black` / `gray` / `green` |
| Language | `language.mode` | enum: 0(ASCII) / 1(EUC-JP) / 2(SJIS) / 3(EUC-KR) / 4(UTF-8) |
| Time | `time.boot_offset_min` | int 0..1439 (분) — 부팅 시각 오프셋 |
| Time | `time.show_seconds` | bool — true 면 시계가 `HH:MM:SS` 와 1초 갱신 |
| About | `about` | action — 정보 안내 |

### Taskbar 윈도우 목록 / Alt+Tab

* taskbar 가운데 영역에 visible 한 app sheet 들이 버튼으로 표시됩니다 (최대 8개).
* focus 중인 창의 버튼은 sunken (눌린) 상태로 표시.
* 버튼 click → 해당 창을 top + focus.
* `Alt+Tab` / `Alt+Shift+Tab` 으로 윈도우 순환 (system widget 제외).
* 단독 `Tab` 은 focused app 으로 char(0x09) 전달 (explorer tree↔list focus 등).

### 시스템 트레이 시계

* 부팅 시각 = `time.boot_offset_min` (기본 0 → `00:00`). RTC 는 사용하지 않습니다.
* 분 단위 갱신, `time.show_seconds=true` 일 때 초 단위 갱신 + `HH:MM:SS` 표시.

### MENU.CFG / SETTINGS.CFG 형식

자세한 스펙은 [`_doc/menu-config.md`](_doc/menu-config.md) 참고. 요약:

```ini
# /SYSTEM/MENU.CFG
[start]
items = Programs, Settings, ---, Run..., About BxOS, ---, Restart, Shutdown

[item:Explorer]
handler = exec:/EXPLORER.HE2
hotkey  = E
```

* 핸들러 URI: `exec:<경로>` / `builtin:<id>` / `settings:<key>` / `submenu:<섹션>`.
* 빌트인 id: `console`, `taskmgr`, `run`, `about`, `shutdown`, `restart`, `settings`, `debug`.
* 잘못된 line 은 skip 되고 `[menu] bad handler: ...` 같은 경고가 **debug 창**에 누적 — `Start → Programs → Debug` 로 확인.

```ini
# /SYSTEM/SETTINGS.CFG
display.background = navy
language.mode = 0
time.boot_offset_min = 0
time.show_seconds = false
```

호스트에서 직접 확인:

```bash
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/SYSTEM
python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/SYSTEM/MENU.CFG /tmp/m.cfg && cat /tmp/m.cfg
python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/SYSTEM/SETTINGS.CFG /tmp/s.cfg && cat /tmp/s.cfg
```
