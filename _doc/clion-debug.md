# CLion + GDB + QEMU 디버깅 가이드

CLion 의 GDB Remote Debugger 로 BxOS 커널과 HE2 앱을 source-level 디버깅하기.
QEMU 의 내장 GDB stub 을 통해 attach 합니다 — 별도 KGDB 패치는 필요 없습니다.

> 핵심 흐름:  Debug 빌드 → `./run-qemu.sh --debug` → CLion 의 "GDB Remote Debug" run config 가 `tcp:1234` 로 attach.

## 0. 사전 준비

```bash
# 1) i686-elf-gdb 설치 (i686-elf-gcc / -binutils 와 같은 채널의 gdb).
brew install i686-elf-gdb

# 2) Debug 빌드 디렉터리를 따로 만든다.
#    (Release 빌드와 분리해 산출물 충돌을 피한다.)
cmake -S . -B build/cmake-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-debug

# 3) DWARF 가 들어갔는지 확인.
i686-elf-readelf -S build/cmake-debug/bootpack.elf | grep debug_info
# → ".debug_info" 가 보이면 OK.
```

| 산출물 | 의미 |
|---|---|
| `build/cmake-debug/bootpack.elf` | 커널 ELF (DWARF 포함, .text VMA=0, .data VMA=0x310000) |
| `build/cmake-debug/haribote.img` / `data.img` | 부팅용 이미지 (Release 와 동일 형식) |
| `build/cmake-debug/he2/obj/<app>/<app>.elf` | HE2 앱 ELF (DWARF 포함, .text VMA=0x40 등) |
| `build/cmake-debug/he2/bin/<app>.he2` | 패킹된 HE2 (런타임이 실제로 로드) |
| `tools/debug/bxos.gdb` | GDB 헬퍼 스크립트 (`bxos-attach`, `bxos-load-app` 등) |

## 1. QEMU 를 GDB stub 모드로 띄우기

```bash
./run-qemu.sh --debug
```

* `--debug` → `-gdb tcp::1234 -S` 추가 (CPU halt + GDB stub).
* `--debug --no-halt` → stub 만 열고 CPU 즉시 실행 (running 상태에서 attach).
* `QEMU_GDB_PORT=2345 ./run-qemu.sh --debug` 로 다른 포트.

QEMU 창이 검은 상태로 멈춰 있으면 정상 — gdb 가 `continue` 를 보내야 부팅이 시작됩니다.

`-S` 가 없으면 OS 가 부팅까지 진행되어 있고, 사용자가 attach 한 시점부터 break/step 가능합니다.

## 2. 커맨드라인 GDB 로 먼저 검증 (선택)

CLion 설정 전에, 한 번은 raw GDB 로 동작을 확인하는 게 빠릅니다.

```bash
# 터미널 A
./run-qemu.sh --debug

# 터미널 B
i686-elf-gdb -x tools/debug/bxos.gdb build/cmake-debug/bootpack.elf
(gdb) bxos-attach
(gdb) break HariMain
(gdb) continue
# → QEMU 창이 부팅을 시작하고 HariMain 진입 시 멈춥니다.
(gdb) info locals
(gdb) next
```

`tools/debug/bxos.gdb` 가 등록하는 helper:

| 명령 | 동작 |
|---|---|
| `bxos-attach [<elf>]` | `target remote :1234`. 기본 ELF = `build/cmake-debug/bootpack.elf` |
| `bxos-breakpoints` | `HariMain`, `console_task`, `start_menu_dispatch`, `fs_data_open_path`, `hrb_api` 에 break |
| `bxos-boot` | real-mode 부팅(asmhead) → protected mode 진입(`0x00280020`) 까지 step |
| `bxos-load-app <app.elf> <base>` | HE2 앱 심볼을 runtime base 주소에 add-symbol-file (§5) |

## 3. CLion Run/Debug Configuration

CLion 메뉴: **Run → Edit Configurations… → `+` → GDB Remote Debug**.

설정 값:

| 항목 | 값 |
|---|---|
| **Name** | `BxOS (QEMU)` |
| **'target remote' args** | `tcp:localhost:1234` |
| **Symbol file** | `$ProjectFileDir$/build/cmake-debug/bootpack.elf` |
| **Sysroot** | (비워둠) |
| **Path mappings** | `Remote: $ProjectFileDir$` ↔ `Local: $ProjectFileDir$` |
| **Initial commands** (optional) | 아래 박스 참고 |
| **GDB binary** | Settings → Build/Execution → Toolchains → Debugger 에서 `/opt/homebrew/bin/i686-elf-gdb` 지정 |

**Initial commands** 에 넣으면 좋은 내용:

```text
source /Users/skshin/git/skshin/bxos/tools/debug/bxos.gdb
set architecture i386
set disassembly-flavor intel
```

> CLion 의 "GDB Remote Debug" 는 `target remote` 를 자동으로 보내므로, `bxos-attach` 안의 `target remote` 와 충돌하지 않게 **Initial commands 에선 `bxos-attach` 자체는 호출하지 않습니다**. `source` 만 해서 helper 명령들을 등록합니다.

### 3.1 GDB toolchain 등록

CLion 의 macOS 기본 LLDB 는 i386 ELF 의 DWARF 를 잘 못 다룹니다. 반드시
`i686-elf-gdb` 를 쓰세요.

1. **CLion → Settings → Build, Execution, Deployment → Toolchains**
2. 새 toolchain 또는 기존 toolchain 의 **Debugger** 를 `Custom GDB` 로 바꾸고
   `/opt/homebrew/bin/i686-elf-gdb` 지정.
3. 같은 화면의 **CMake** 항목은 시스템 cmake 그대로.

### 3.2 빌드 / 실행 워크플로우

CLion 에서 직접 QEMU 를 띄우려면 **External Tool** 을 만드는 것이 편합니다.

* **Settings → Tools → External Tools → `+`**
  * Name: `QEMU --debug`
  * Program: `$ProjectFileDir$/run-qemu.sh`
  * Arguments: `--debug`
  * Working directory: `$ProjectFileDir$`
  * (체크) **Synchronize files after execution** 끄기 — QEMU 가 멈춰 있는 동안 CLion 이 응답성을 잃을 수 있어 보통 별도 터미널이 더 편합니다.

이후 흐름:

1. `Tools → External Tools → QEMU --debug` (또는 별도 터미널 `./run-qemu.sh --debug`).
2. CLion 에서 `BxOS (QEMU)` run config 의 **Debug** 버튼 클릭.
3. CLion 이 `i686-elf-gdb` 를 띄워 `tcp:1234` 로 attach. `bootpack.elf` 의 심볼이 자동 로드.
4. 소스 파일 (e.g. `harib27f/haribote/bootpack.c`) 의 line gutter 를 클릭해 break point 설정.
5. CLion debugger 의 **Resume Program** (F9) 으로 부팅 시작.

### 3.3 Path mapping (선택)

만약 ELF 안에 절대 경로가 들어 있는데 CLion 이 소스를 못 찾는다면 `info source`
로 확인하고 Path mapping 을 추가:

```
Remote: /Users/skshin/git/skshin/bxos
Local:  $ProjectFileDir$
```

보통 동일 머신에서 빌드한 ELF 라 매핑은 필요 없습니다.

## 4. 커널 로드 주소에 대한 메모

* **bootpack.elf**: 링커 스크립트 [`tools/modern/linker-bootpack.lds`](../tools/modern/linker-bootpack.lds) 에서
  `.text` VMA = `0x00000000`, `.data` VMA = `0x00310000`.
* 런타임에는 `asmhead.nas` 가 bootpack 을 `BOTPAK = 0x00280000` 로 복사하고
  GDT 의 selector 2*8 (CS) 의 base 를 `0x00280000` 으로 설정한 뒤 jump.
* 그 결과 코드 linear address = `0x00280000 + EIP_offset`. **GDB / QEMU stub 은
  CS:EIP 를 그대로 EIP=offset 으로 보고**하므로 `bootpack.elf` 의 .text VMA(0)
  와 EIP 값이 자연스럽게 매칭됩니다 (별도 `add-symbol-file` 오프셋 불필요).
* `.data` 는 ELF VMA 가 이미 `0x00310000` 이므로 마찬가지로 그대로 일치.

따라서 CLion 의 **Symbol file** 에 `bootpack.elf` 만 지정해도 source-level
breakpoint, watchpoint, locals 모두 동작합니다.

## 5. HE2 앱 디버깅

HE2 앱은 `api_exec` 이 호출될 때 **kernel 의 `memman_alloc_4k` 가 잡아주는
주소** 에 적재됩니다. 매 실행마다 다를 수 있어 정적 매핑이 어렵습니다.

### 5.1 HE2 앱 base 주소 알아내기

커널 측 break point 로 먼저 멈추고, 앱의 segment base 를 읽습니다.

```text
(gdb) break console.c:hrb_api_exec_inner_or_nearby     ← exec 진입 지점
(gdb) continue
... QEMU 안에서 explorer 실행 ...
(gdb) print task->tss.cs       ← LDT/GDT selector
(gdb) print *((uint32_t*)0x270000 + 8 + ((task->tss.cs >> 3) << 1))
  ← 또는 더 간단히 console.c 가 base 변수를 쓰는 라인의 값 확인
```

실제로는 더 쉬운 방법이 있습니다 — `task->ds_base` 처럼 커널이 들고 있는
load address 변수를 직접 읽으세요. console.c 의 `cmd_app` / `api_exec` 경로를
보면 exec 한 직후 `ds_base` (또는 같은 의미의 변수) 가 정해집니다. 거기에
break 를 걸면 바로 base 가 보입니다.

### 5.2 HE2 앱 심볼 add

```text
(gdb) bxos-load-app build/cmake-debug/he2/obj/explorer/explorer.elf 0x12345000
[bxos] app symbols loaded at 0x12345000
(gdb) break explorer.c:tree_focus_set
(gdb) continue
```

* HE2 앱은 코드 + 데이터를 같은 base 에 올리므로 `add-symbol-file <elf> <base>`
  하나로 충분합니다.
* 앱 종료 시 `remove-symbol-file -a <base>` 로 해제. 다음에 같은 앱이 다시
  로드되면 base 가 바뀔 수 있으니 다시 add.

### 5.3 HE2 앱 source path

CLion 에서 path mapping 이 필요 없는 경우가 대부분이지만, 만약 `harib27f/<app>`
와 `he2/apps/<app>` 두 트리에 같은 이름 앱이 있다면 **현재 빌드되는 쪽
(`harib27f/<app>`)** 의 경로가 ELF 에 들어 있는지 `i686-elf-objdump --dwarf=decodedline ...`
로 확인하세요.

## 6. 자주 만나는 문제

| 증상 | 원인 / 해결 |
|---|---|
| CLion 에 line breakpoint 가 회색으로 표시됨 | Debug 빌드가 아닌 ELF 를 symbol file 로 지정. `build/cmake-debug/bootpack.elf` 사용 확인. |
| `target remote` 가 `Connection refused` | QEMU 가 `--debug` 없이 떠 있음. `--debug` 다시 실행하거나 `QEMU_GDB_PORT` 일치 확인. |
| `(gdb) c` 직후 QEMU 가 무반응 | `-S` 없이 attach 한 상태에서 OS 가 이미 idle. `Pause Program` (F8) → 다시 step. |
| `info locals` 에 변수가 `<optimized out>` | Release 빌드 (`-O2`). Debug 빌드 (`-O0 -g`) 사용. |
| `info source` 에서 file path 가 absolute 라 CLion 이 소스를 못 찾음 | Path mapping 추가 (§3.3) 또는 같은 워크트리에서 빌드. |
| `.text` 에 break 해도 kernel 진입 직전(`asmhead`)에서 안 잡힘 | asmhead.nas (real mode) 는 DWARF 가 없음. `bxos-boot` 로 protected mode 진입까지 step → 그 뒤로 정상. |
| LLDB 가 잡힘 (i386 DWARF 못 읽음) | CLion → Settings → Toolchains → Debugger 를 **Custom GDB** = `i686-elf-gdb` 로 변경. |
| `info reg` 가 `Bad register` | QEMU 의 reset 직후 일부 reg 가 invalid. `c` 한 번 해서 protected mode 까지 진행. |

## 7. 응용

* **콘솔 명령 트레이스**: `break cons_runcmd` → 콘솔에서 명령 입력 → 명령 dispatcher 로 진입.
* **Settings 저장 검증**: `break settings_apply_pair` → Settings 에서 값 변경 → 어떤 key/value 로 호출되는지 확인.
* **Start menu hotkey 매칭**: `break menu_try_hotkey` → 메뉴 열고 글자 누름 → 매칭 동작 step-through.
* **메모리 누수 추적**: `watch g_clock_seconds` 등 watchpoint 도 동작.

## 8. 참고

* QEMU GDB stub 공식 문서: <https://qemu-project.gitlab.io/qemu/system/gdb.html>
* CLion Remote Debug: <https://www.jetbrains.com/help/clion/remote-debug.html>
* BxOS 부팅 시퀀스 / 메모리 맵: [`_doc/storage.md`](storage.md), [`tools/modern/linker-bootpack.lds`](../tools/modern/linker-bootpack.lds)
* 헬퍼 스크립트: [`tools/debug/bxos.gdb`](../tools/debug/bxos.gdb)
