# BxOS · macOS Apple Silicon 셋업 가이드

옛 Windows 빌드 환경에서 만들어진 `bxos` (Haribote OS 한국어 변형) 을 현재의 Apple Silicon Mac 에서 빌드·부팅하기 위한 단계별 안내입니다.

work1/work2 작업 이후 빌드 시스템은 **CMake + NASM + i686-elf-gcc + Python** 으로 정리되었고, 디스크 이미지는 **부팅 FDD + 데이터 HDD** 두 갈래로 분리됩니다. 데이터 HDD 는 서브디렉터리와 `/` 기준 경로를 지원합니다.

| 이미지 | 위치 | 내용 |
|---|---|---|
| 부팅 FDD (FAT12 1.44MB) | `build/cmake/haribote.img` | `HARIBOTE.SYS` + `NIHONGO.FNT` + `HANGUL.FNT` |
| 데이터 HDD (FAT16 32MB) | `build/cmake/data.img`     | HE2 앱 25개 + 데모 데이터 10개 |

자세한 디스크/드라이브 구조는 [`_doc/storage.md`](_doc/storage.md), 콘솔 명령은 [`BXOS-COMMANDS.md`](BXOS-COMMANDS.md) 참고.

---

## 0. 사전 확인

```bash
uname -m            # arm64 → Apple Silicon, x86_64 → Intel Mac
sw_vers             # macOS 버전
```

Homebrew 가 없다면 먼저 설치:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

`brew --prefix` 출력이 `/opt/homebrew` 면 Apple Silicon 네이티브, `/usr/local` 이면 Intel/Rosetta.

---

## 1단계 — 부팅만 해보기 (5분)

QEMU 만 있으면 됩니다.

```bash
brew install qemu

cd ~/git/skshin/bxos
chmod +x run-qemu.sh

# 빌드 산출물(build/cmake/haribote.img)이 있으면 그것을, 없으면
# 트리에 동봉된 harib27f/haribote.img 폴백을 부팅합니다.
./run-qemu.sh
```

QEMU 윈도우가 뜨면서 BxOS 데스크톱이 나타나야 합니다. Apple Silicon 이라도 `qemu-system-i386` 은 x86 을 TCG(software emulation) 로 돌리므로 약간 느리지만 정상 동작.

`build/cmake/data.img` 가 존재하면 자동으로 `-hda` 로 같이 부착됩니다. 끄고 싶으면:

```bash
./run-qemu.sh --no-data        # 데이터 디스크 없이 부팅
./run-qemu.sh --data path.img  # 데이터 디스크 명시
```

### 자주 쓰는 옵션

```bash
QEMU_MEM=128 ./run-qemu.sh                                  # 메모리 늘리기
QEMU_EXTRA="-d int -no-reboot -no-shutdown" ./run-qemu.sh   # 디버깅용 인터럽트 추적
QEMU_EXTRA="-display cocoa,full-screen=on" ./run-qemu.sh    # 풀스크린
```

### 한글 출력 테스트

부팅 후 콘솔에서 아래 명령으로 한글 출력을 시험해 볼 수 있습니다:

* **EUC-KR 모드**: `langmode 3` 입력 후 `type hangul.euc` 실행.
* **UTF-8 모드**: `langmode 4` 입력 후 `type hangul.utf8` 또는 `khello` 실행.

### 트러블슈팅

* **"qemu-system-i386: command not found"** — `brew install qemu` 가 끝났는지 확인. `which qemu-system-i386`.
* **마우스 포인터가 윈도우 안에서 안 잡힘** — QEMU 메뉴 또는 `Ctrl+Alt+G` 로 토글.
* **종료** — QEMU 창의 X 버튼 또는 `Ctrl+Alt+Q`.

---

## 2단계 — CMake 로 전체 빌드

원본 `z_tools/*.exe` (Wine 의존) 를 거치지 않고, 현대 툴체인만으로 커널 + 앱 + 디스크 이미지를 만듭니다.

### 2.1 사전 설치

```bash
brew install nasm cmake qemu

# i686-elf-gcc 가 brew 에 있으면 ↓ 우선
brew install i686-elf-gcc i686-elf-binutils

# 폴백 (CMake 가 자동 감지·전환):
brew install x86_64-elf-gcc x86_64-elf-binutils
```

설치 확인:

```bash
nasm -v
i686-elf-gcc --version || x86_64-elf-gcc --version
cmake --version
```

### 2.2 빌드

```bash
# 한 번만: CMake configure
cmake -S . -B build/cmake

# 전체 빌드 (kernel-img + data-img 둘 다)
cmake --build build/cmake

# 산출물
ls build/cmake/haribote.img build/cmake/data.img
```

### 2.3 개별 타겟

```bash
cmake --build build/cmake --target kernel-img    # haribote.img (FDD) 만
cmake --build build/cmake --target data-img      # data.img (HDD) 만
cmake --build build/cmake --target run           # 빌드 + qemu 부팅
cmake --build build/cmake --target info          # 컴파일러/도구 경로 확인

# 한 앱만 다시 빌드해서 data.img 에 부분 갱신:
cmake --build build/cmake --target install-tetris
cmake --build build/cmake --target install-winhelo
# … 모든 HE2 앱에 install-<name> 헬퍼가 자동 등록됨.
```

`install-<app>` 은 `data-img` 전체를 다시 안 만들고, 호스트 도구 [`tools/modern/bxos_fat.py`](tools/modern/bxos_fat.py) 로 기존 `data.img` 안의 해당 파일만 교체합니다. 앱 하나 고치고 재부팅 사이클이 빠릅니다.

### 2.4 부팅

```bash
./run-qemu.sh
# 또는 CMake 로
cmake --build build/cmake --target run
```

### 2.5 검증

```bash
# 이미지 형식 확인
file build/cmake/haribote.img build/cmake/data.img

# FAT 무결성 확인 (macOS 기본 도구)
fsck_msdos -n build/cmake/haribote.img
fsck_msdos -n build/cmake/data.img

# data.img 안 파일 목록
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/

# data.img 안 서브디렉터리/파일 조작
python3 tools/modern/bxos_fat.py mkdir build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py cp HOST:build/cmake/he2/bin/tetris.he2 build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py rm build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py rmdir build/cmake/data.img:/sub
```

---

## 3. 알려진 이슈 / 수정 이력

CMake 빌드는 내부적으로 NASM 입력 변환 / HRB 헤더 / 폰트 / FAT 이미지 도구를 [`tools/modern/`](tools/modern/) 의 Python 스크립트로 호출합니다. 그 과정에서 옛 코드 / 옛 빌드체인과 차이가 났던 부분들:

#### (a) `instruction expected, found '...'` (한글 주석 줄)

원본 `ipl09.nas` 에 `;` 가 빠진 한글 주석 줄이 한 군데 있어요. nask 는 non-ASCII 로 시작하는 줄을 묵시적 주석으로 받아주지만 NASM 은 엄격합니다. `nas2nasm.py` 가 `0x80` 이상으로 시작하는 줄에 자동으로 `;` 를 붙여 처리합니다.

#### (b) `non-constant argument supplied to TIMES` / RESB 패딩

`RESB 0x7dfe-$` 같이 `$` (current address) 가 포함된 RESB 표현식은 NASM 에서 안 됩니다 (BSS 전용). flat 바이너리 패딩이라 `TIMES ... db 0` 으로 바꾸되, `ORG 0x7c00` 이 섞인 절대주소 식은 NASM 이 상수로 못 보므로 `TIMES 0x1fe-($-$$) db 0` 처럼 섹션 시작 기준 offset 으로 변환합니다. `nas2nasm.py` 가 식이 들어간 RESB 만 자동 변환합니다.

#### (c) `stdio.h` / `sprintf` / `strcmp` 관련 오류

원본 빌드는 `z_tools/haribote` 헤더와 `golibc.lib` 를 obj2bim 규칙에서 함께 링크합니다. CMake 빌드는 해당 라이브러리를 직접 링크하지 않고, `z_tools/haribote` 헤더를 보조 include 경로로 두며 커널이 실제 쓰는 `sprintf`/문자열 함수는 [`tools/modern/modern_libc.c`](tools/modern/modern_libc.c) 로 제공합니다.

#### (d) 앱 실행 시 `Bad command or file name.`

`A.HE2` 같은 파일이 `data.img` 에 있어도 검색이 실패한다면 `fs_data_search()` 와 8.3 이름 매칭을 의심합니다. FAT 8.3 이름은 `name[8] + ext[3]` 구조인데 원본 코드는 `name[0..10]` 처럼 확장자까지 연속 배열로 읽습니다. 현대 GCC는 이걸 배열 범위 밖 접근으로 최적화할 수 있어서, 구조체 전체를 바이트 포인터로 보고 11바이트를 비교하도록 수정해 두었습니다.

`disk` 명령으로 ATA 인식 / `dir` 명령으로 루트 목록부터 확인하세요. data.img 가 안 마운트되면 `dir` 도 비어 있게 나옵니다.

#### (e) `undefined reference to '_io_hlt'` 또는 `'io_hlt'`

심볼 underscore 컨벤션 차이입니다. 옛 `cc1.exe` (gcc 1.x) 는 C 심볼 앞에 자동으로 `_` 를 붙이지만 modern `i686-elf-gcc` (ELF 타깃) 는 안 붙입니다. 반면 `naskfunc.nas` 는 `_io_hlt` 처럼 `_` 가 박힌 심볼을 export 합니다.

CMake 가 `-fleading-underscore` 를 gcc 에 넘겨 옛 컨벤션을 유지하므로 둘 다 `_io_hlt` 로 매칭됩니다. 해당 옵션을 거부하는 컴파일러를 만나면 `naskfunc.nas` 의 `_` 접두를 일괄 제거하는 방식으로 폴백할 수 있습니다.

#### (f) gcc 가 옛 K&R 스타일 코드에 경고 / 에러

CMake 가 `-Wno-implicit-function-declaration -Wno-int-conversion -Wno-pointer-sign -Wno-incompatible-pointer-types` 를 미리 추가해 두었습니다. 그래도 막히는 곳이 있으면 메시지를 그대로 채팅에 붙여 주세요.

#### (g) `bootpack.elf` 가 0 바이트 / 링크 에러

[`tools/modern/linker-bootpack.lds`](tools/modern/linker-bootpack.lds) 의 ENTRY 지정이 안 맞거나 startup 의 `.text.startup` 섹션이 안 들어왔을 수 있음. `objdump -h build/cmake/bootpack.elf` 로 섹션 배치 확인. `.text` 는 VMA 0 부터, `.data` 는 VMA `0x00310000` 부근부터 시작해야 정상.

#### (h) 부팅은 되는데 화면이 깨짐 / 멈춤

데이터 세그먼트 복사 문제일 가능성이 큼. `xxd -g1 -l 32 build/cmake/bootpack.hrb` 에서 `data_size`(0x10) 와 `data_file`(0x14) 이 0 이 아니어야 하고, `esp_init`(0x0c) 은 보통 `00 00 31 00` (`0x310000`) 이어야 합니다.

자세한 설계 노트는 [`tools/modern/README.md`](tools/modern/README.md) 와 [`_doc/`](_doc/) 의 문서들 참고.

---

## 부록 — 디렉터리 정리

work1/work2 작업 후 트리 구조:

```
bxos/
├── README.utf8.md                # README EUC-KR → UTF-8 사본
├── SETUP-MAC.md                  # 이 문서
├── BXOS-COMMANDS.md              # 콘솔 명령 / 포함 앱 레퍼런스
├── CMakeLists.txt                # 빌드 진입점 (kernel-img + data-img)
├── run-qemu.sh                   # QEMU 실행 래퍼 (data.img 자동 부착)
├── build/cmake/                  # 빌드 산출물 (.gitignore)
│   ├── haribote.img              # 부팅 FDD (FAT12 1.44MB)
│   ├── data.img                  # 데이터 HDD (FAT16 32MB)
│   ├── haribote.sys
│   └── he2/bin/*.he2
├── cmake/                        # CMake 툴체인 / HariboteApp 헬퍼
├── harib27f/                     # 커널 + 폰트 + 데모 데이터 + legacy HRB 앱
│   └── haribote/                 # 커널 소스 (ata.c, fs_fat.c, console.c, …)
├── he2/                          # HE2 앱 트리 (현재 이미지에 들어가는 앱들)
│   ├── apps/                     # winhelo, tetris, type, echo, touch, fdel, …
│   └── libbxos/                  # HE2 syscall wrapper
├── tools/
│   └── modern/                   # NASM + gcc + Python 빌드 도구
│       ├── nas2nasm.py           # nask → NASM 패치
│       ├── hrbify.py             # 32B HRB 헤더 + 0x1B JMP 패치
│       ├── makefont.py           # hankaku.txt → 폰트 바이너리
│       ├── mkfat12.py            # FAT12/FAT16 이미지 생성
│       ├── bxos_fat.py           # data.img 부분 갱신 (create/ls/cp/rm/mkdir/rmdir)
│       ├── linker-bootpack.lds   # GNU ld 링커 스크립트
│       ├── startup_kernel.s      # 커널 진입점 + 32B 헤더 자리
│       ├── modern_libc.c         # sprintf/strcmp 등 freestanding 보조
│       └── Makefile.modern       # legacy GNU make 빌드 (CMake 와 별도, 참고용)
├── z_tools/, z_osabin/, z_new_o/, z_new_w/   # 원본 Windows 툴체인 (현재 빌드 미사용)
└── _doc/                         # 설계/인수인계 문서
    ├── storage.md                # 드라이브 모델 / FAT16 / ATA
    ├── work1.md                  # 쓰기 가능 FS 도입 작업 계획
    └── work1-handoff.md          # 인수인계 노트
```

원본 트리(`harib27f/`, `z_tools/`, `z_osabin/`, `z_new_*`) 의 소스/도구는 그대로 두고, 빌드 시스템만 CMake + Python 으로 갈아탔습니다.
