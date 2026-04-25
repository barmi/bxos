# BxOS 콘솔 명령어 / 포함 앱 빠른 참조

이 문서는 `./run-qemu.sh build/modern/haribote.img` 로 부팅한 뒤 콘솔 창에서 입력할 수 있는 명령을 정리합니다.

명령 이름은 기본적으로 소문자로 입력하는 것을 권장합니다. 앱 파일은 FAT 8.3 이름으로 저장되어 있으며, `.hrb` 확장자는 생략할 수 있습니다. 예를 들어 `winhelo` 와 `winhelo.hrb` 는 같은 앱을 실행합니다.

## 기본 명령어

| 명령 | 설명 |
|---|---|
| `mem` | 전체 메모리와 사용 가능한 메모리를 표시합니다. |
| `cls` | 현재 콘솔 창의 표시 영역을 지웁니다. |
| `dir` | 플로피 이미지 루트 디렉터리의 파일 목록을 표시합니다. |
| `task` | 실행 중인 태스크 목록을 Debug Window에 표시합니다. |
| `taskmgr` | 태스크 매니저 창을 엽니다. |
| `exit` | 현재 콘솔 태스크를 종료합니다. 기본 콘솔 창에서 쓰면 창이 닫힐 수 있습니다. |
| `start <명령>` | 새 콘솔 창을 열고 그 안에서 `<명령>`을 실행합니다. 예: `start dir`, `start winhelo` |
| `ncst <명령>` | 콘솔 창 없이 새 콘솔 태스크를 만들고 `<명령>`을 실행합니다. 창을 직접 여는 앱 실행에 유용합니다. 예: `ncst winhelo` |
| `langmode 0` | ASCII 모드로 전환합니다. |
| `langmode 1` | 일본어 EUC 계열 모드로 전환합니다. |
| `langmode 2` | 일본어 Shift-JIS 계열 모드로 전환합니다. |

## 앱 실행 방법

콘솔에서 파일명만 입력하면 됩니다.

```text
a
hello3
winhelo
calc 1+2*3
gview fujisan.jpg
mmlplay kirakira.mml
```

파일 목록은 `dir` 로 확인할 수 있습니다. 앱 실행 시 `Bad command or file name.` 이 나오면 파일명이 틀렸거나 이미지에 해당 파일이 없는 것입니다.

## 포함 앱

| 명령 | 설명 |
|---|---|
| `a` | 콘솔에 `A`를 출력합니다. |
| `hello3`, `hello4`, `hello5` | 콘솔에 hello 메시지를 출력하는 예제입니다. |
| `winhelo`, `winhelo2`, `winhelo3` | 작은 창을 열어 hello 메시지를 표시합니다. Enter로 종료합니다. |
| `star1`, `stars`, `stars2` | 점/별을 창에 그리는 그래픽 예제입니다. Enter로 종료합니다. |
| `lines` | 여러 색상의 선을 그립니다. Enter로 종료합니다. |
| `walk` | `*` 문자를 커서키로 움직이는 예제입니다. Enter로 종료합니다. |
| `noodle` | 타이머 카운트다운 예제입니다. |
| `beepdown` | 비프음 주파수를 낮추는 예제입니다. 키를 누르면 종료합니다. |
| `color`, `color2` | 색상 팔레트/그라데이션 표시 예제입니다. 키를 누르면 종료합니다. |
| `sosu`, `sosu2`, `sosu3` | 소수(prime number)를 계산해 콘솔에 출력합니다. |
| `type <파일>` | 텍스트 파일 내용을 콘솔에 출력합니다. 예: `type euc.txt` |
| `iroha` | 일본어 문자열 출력 예제입니다. `langmode`와 함께 확인하면 좋습니다. |
| `chklang` | 현재 언어 모드를 출력합니다. |
| `notrec` | 비직사각형 창 표시 예제입니다. Enter로 종료합니다. |
| `bball` | 공/선 그래픽 예제입니다. Enter로 종료합니다. |
| `invader` | 인베이더 게임 예제입니다. 커서키와 Space를 사용합니다. |
| `calc <식>` | 정수 계산기입니다. 예: `calc 123+0x10`, `calc (5+3)*4` |
| `tview <파일> [-w30 -h10 -t4]` | 텍스트 파일 뷰어입니다. 예: `tview ipl09.nas`, `tview euc.txt -w40 -h15` |
| `mmlplay <파일.mml>` | MML 음악 파일을 재생합니다. 예: `mmlplay kirakira.mml`, `mmlplay daigo.mml`, `mmlplay daiku.mml` |
| `gview <그림파일>` | BMP/JPEG 뷰어입니다. 예: `gview fujisan.jpg`, `gview night.bmp` |

## 포함 데이터 파일

| 파일 | 용도 |
|---|---|
| `euc.txt` | 텍스트/언어 모드 테스트용 파일입니다. |
| `kirakira.mml`, `daigo.mml`, `daiku.mml` | `mmlplay` 로 재생할 수 있는 음악 데이터입니다. |
| `fujisan.jpg`, `night.bmp` | `gview` 로 볼 수 있는 이미지 파일입니다. |
| `nihongo.fnt` | 커널이 부팅 중 읽는 일본어 폰트 파일입니다. |

## 입력 팁

창을 여는 앱은 실행 후 Enter를 눌러 닫는 경우가 많습니다. 일부 앱은 커서키, Space, 숫자 키를 사용합니다.

기본 콘솔에서 앱을 실행하면 콘솔이 앱 종료를 기다릴 수 있습니다. 앱을 별도 콘솔에서 실행하려면 `start <앱명>`, 창만 필요한 앱을 조용히 실행하려면 `ncst <앱명>`을 사용해 보세요.
