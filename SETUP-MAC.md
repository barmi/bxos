# BxOS · macOS Apple Silicon 셋업 가이드

이 문서는 옛 Windows 빌드 환경에서 만들어진 `bxos` (Haribote OS 한국어 변형)을 현재의 Apple Silicon Mac에서 GUI까지 띄워서 동작시키기 위한 단계별 안내입니다.

세 단계로 진행합니다.

1. **이미 빌드된 이미지를 즉시 부팅** — QEMU만 설치하면 끝.
2. **Wine으로 원본 툴체인을 그대로 호출해서 빌드** — 코드 수정/재빌드가 필요할 때.
3. **(선택) Wine 의존을 제거한 현대 툴체인** — NASM·i686-elf-gcc + 자체 Python 도구.

---

## 0. 사전 확인

```bash
uname -m            # arm64 → Apple Silicon, x86_64 → Intel Mac
sw_vers             # macOS 버전
```

Homebrew가 없다면 먼저 설치:

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

`brew --prefix` 출력이 `/opt/homebrew` 면 Apple Silicon 네이티브, `/usr/local` 이면 Intel/Rosetta 입니다.

---

## 1단계 — 즉시 부팅 (5분)

```bash
# QEMU 설치 (i386 에뮬레이션 포함)
brew install qemu

# 저장소 위치로 이동
cd ~/git/skshin/bxos

# 한 번만: 실행 권한
chmod +x run-qemu.sh

# 플로피로 부팅
./run-qemu.sh
# 또는 ISO로
./run-qemu.sh iso
```

QEMU 윈도우가 뜨면서 BxOS 데스크톱이 나타나야 합니다. Apple Silicon이라도 `qemu-system-i386`은 x86을 TCG(software emulation)로 돌리므로 괜찮게 동작합니다(약간 느림).

### 자주 쓰는 옵션

```bash
QEMU_MEM=128 ./run-qemu.sh                                  # 메모리 늘리기
QEMU_EXTRA="-d int -no-reboot -no-shutdown" ./run-qemu.sh   # 디버깅용 인터럽트 추적
QEMU_EXTRA="-display cocoa,full-screen=on" ./run-qemu.sh    # 풀스크린
```

### 트러블슈팅

* **"qemu-system-i386: command not found"** — `brew install qemu` 가 끝났는지 확인. `which qemu-system-i386`.
* **마우스 포인터가 윈도우 안에서 안 잡힘** — QEMU 메뉴 또는 `Ctrl+Alt+G` 로 토글.
* **종료** — QEMU 창의 X 버튼 또는 `Ctrl+Alt+Q`.

---

## 2단계 — Wine으로 원본 툴체인 빌드 (10~30분)

`z_tools/` 안의 `cc1.exe`, `nask.exe`, `obj2bim.exe`, `bim2hrb.exe`, `edimg.exe` 등이 모두 32-bit Windows PE 콘솔 도구라 Wine으로 그대로 돌릴 수 있습니다.

### 2.1 Wine 설치 (Apple Silicon 권장 경로)

```bash
# 권장: wine-crossover (Apple Silicon에서 가장 안정적)
brew tap gcenx/wine
brew install --cask --no-quarantine wine-crossover

# 또는 Rosetta 기반 wine-stable (조금 더 호환성 있지만 Rosetta 필요)
# softwareupdate --install-rosetta --agree-to-license
# brew install --cask wine-stable
```

설치 후 한 번만 prefix 초기화:

```bash
WINEPREFIX=$HOME/.wine-bxos wine wineboot --init
```

방화벽이나 Gatekeeper 경고가 뜨면 `시스템 설정 → 개인정보 보호 및 보안 → 보안` 에서 허용.

### 2.2 빌드

```bash
chmod +x build-mac.sh tools/wine-wrappers/_run.sh tools/wine-wrappers/install-symlinks.sh

# 전체 빌드 (커널 + 모든 앱 + haribote.img)
./build-mac.sh

# 커널만
./build-mac.sh haribote/

# 특정 앱(예: a/) 만
./build-mac.sh app a/

# 청소
./build-mac.sh clean
```

`build-mac.sh` 가 하는 일:

1. `tools/wine-wrappers/` 에 `nask`, `cc1`, `edimg` 등 이름의 심볼릭 링크를 만들어 모두 `_run.sh` 를 가리키게 함.
2. `_run.sh` 는 `$0` 의 basename으로 `z_tools/<name>.exe` 를 결정해 `wine` 으로 실행.
3. PATH 앞에 wrapper 디렉터리와 `tools/shim/` (DOS의 `copy`/`del` 흉내)을 추가.
4. 시스템 GNU `make` 를 호출하면서 Makefile 변수만 override 해서 `$(NASK)`, `$(CC1)` 등이 wrapper 이름을 가리키게 함.

원본 Makefile은 한 글자도 안 고치고 그대로 둡니다.

### 2.3 빌드 결과 부팅

```bash
./run-qemu.sh                       # 새로 만들어진 harib27f/haribote.img 자동 사용
```

### 2.4 트러블슈팅

* **"wine: command not found"** — `which wine` / `which wine64` 확인. wine-crossover는 일반적으로 `/Applications/Wine Crossover.app/Contents/Resources/wine/bin/wine` 에 있음. PATH에 추가하거나 `WINE=/full/path/to/wine` 으로 지정.
* **첫 실행 시 wineserver가 멈춰 있음** — `WINEPREFIX=$HOME/.wine-bxos wineserver -k` 후 재시도.
* **인코딩 깨짐(빌드 출력)** — `z_tools/*.exe`는 일본어/한국어 출력을 SJIS/EUC-KR로 내보내는데 Wine 콘솔 출력이 깨질 수 있습니다. 결과물(`*.img`/`*.hrb`)에는 영향 없음.
* **'cc1: 0xC0000005'** 같은 segfault — Apple Silicon의 wine-crossover에서 가끔 발생. `WINEDEBUG=fixme-all` 로 재시도하거나, Rosetta 기반 wine-stable로 갈아끼우기.
* **`copy`/`del` not found** — `build-mac.sh` 가 만드는 `tools/shim/` 가 PATH에 끼어있지 않음. 직접 호출 말고 `./build-mac.sh` 로 진입할 것.
* **`make.exe`가 호출됨** — Makefile 안의 재귀 호출이 `$(MAKE)` 가 아닌 `../z_tools/make.exe` 로 박혀있는 곳이 있다면, 해당 라인을 `$(MAKE)` 로 고치거나 `MAKE` override 변수가 propagate 되도록 `export MAKE`.

---

## 3단계 — Wine 의존 제거 (NASM + i686-elf-gcc + Python)

원본 `z_tools/*.exe` 를 한 개도 부르지 않고 빌드합니다. 앱 `.hrb` 파일들은 OSASKCMP 압축이라 변환이 비현실적이라 기존 빌드본을 그대로 디스크 이미지에 복사합니다(원본 트리에 이미 빌드되어 있음). 커널 + 부트로더 + asmhead 는 처음부터 다시 빌드.

### 3.1 사전 설치

```bash
brew install nasm qemu

# i686-elf-gcc 가 brew 에 있으면 ↓ 우선
brew install i686-elf-gcc i686-elf-binutils

# 폴백 (Makefile.modern 가 자동 감지·전환):
brew install x86_64-elf-gcc x86_64-elf-binutils
```

설치 확인:

```bash
nasm -v
i686-elf-gcc --version || x86_64-elf-gcc --version
```

### 3.2 빌드

```bash
chmod +x build-modern.sh
./build-modern.sh info        # 도구/경로 확인
./build-modern.sh             # build/modern/haribote.img 까지 한번에
./run-qemu.sh build/modern/haribote.img
```

### 3.3 단계별 빌드 (실패 시 어디서 막혔는지 좁힐 때)

```bash
make -f tools/modern/Makefile.modern build/modern/ipl09.bin       # NASM 만
make -f tools/modern/Makefile.modern build/modern/asmhead.bin
make -f tools/modern/Makefile.modern build/modern/naskfunc.o
make -f tools/modern/Makefile.modern build/modern/bootpack.o      # gcc
make -f tools/modern/Makefile.modern build/modern/bootpack.elf    # ld 링크
make -f tools/modern/Makefile.modern build/modern/bootpack.hrb    # HRB 헤더
make -f tools/modern/Makefile.modern build/modern/haribote.sys    # 합침
make -f tools/modern/Makefile.modern build/modern/haribote.img    # 디스크
```

### 3.4 동작/구조 비교

```bash
# 원본과 사이즈/구조 비교
ls -la harib27f/haribote.img build/modern/haribote.img
file harib27f/haribote.img build/modern/haribote.img

# 부트섹터(IPL) 비교 — nas2nasm 변환·재어셈블이 byte-identical 인지
cmp -n 512 harib27f/haribote.img build/modern/haribote.img && echo "boot OK"
```

### 3.5 자주 발생할 만한 문제

* **NASM 어셈블 에러 `parser: instruction expected`** — `nas2nasm.py` 가 잡지 못한 nask 디렉티브가 있는 경우. 해당 줄을 보고 수동으로 주석 처리하거나 `nas2nasm.py` 에 규칙 추가.
* **`undefined reference to '_io_hlt'`** — nask 어셈블리는 심볼 앞에 `_` 를 강제로 붙이지만 i686-elf-gcc 는 `_` 없이 export 합니다. `nas2nasm.py` 에 `s/^_(\w+)/\1/` 규칙을 추가하거나, 임시로 `naskfunc.nasm.nas` 의 GLOBAL 선언만 일괄 치환:

  ```bash
  sed -i '' 's/_\([a-z_]\+\)/\1/g' build/modern/naskfunc.nasm.nas
  ```

  이때 ASCII 식별자만 영향을 받게 정확한 치환을 사용해야 함. 그 후 `make -f tools/modern/Makefile.modern build/modern/naskfunc.o` 재실행.

* **`error: implicit declaration of function`** 류 경고가 에러로 — `Makefile.modern` 에서 `-Werror` 는 끈 상태이지만 만약 가족이 들어와 있다면 제거. 또는 `CFLAGS` 에 `-Wno-implicit-function-declaration -Wno-int-conversion -Wno-pointer-sign` 추가.

* **`bootpack.elf` 가 0 바이트 / 링크 에러** — `linker-bootpack.lds` 의 ENTRY 지정이 안 맞거나 startup 의 `.text.startup` 섹션이 안 들어왔을 수 있음. `objdump -h build/modern/bootpack.elf` 로 섹션 배치 확인. 첫 32바이트가 0, 그 다음 `_HariStartup` 의 push/mov/pop/jmp 가 있어야 정상.

* **부팅은 되는데 화면이 깨짐 / 멈춤** — 메모리 레이아웃 문제. `hrbify.py` 의 `--stack-top 3136k --esp-init 3136k` 가 원본 `obj2bim ... stack:3136k` 와 일치해야 함. 조정해 보고 동작 변화 관찰.

자세한 트러블슈팅과 빌드 흐름 그림은 [`tools/modern/README.md`](tools/modern/README.md) 참고.

---

## 부록 A — Cowork(이 채팅)와 작업 분담

Cowork에서 직접 처리한 것:

* 이 가이드 문서, `run-qemu.sh`, `build-mac.sh`, Wine wrapper들, `tools/modern/*.py` 작성
* `mkfat12.py`/`makefont.py` 동작 검증(샌드박스 Linux에서)
* README 인코딩 분석 및 UTF-8 사본 (`README.utf8.md`)

Cowork가 못 하는 것 (Mac에서 직접 해야 함):

* `brew install qemu`, `brew install --cask wine-crossover` 등 시스템 설치
* macOS 화면에 QEMU GUI 창 띄우기(샌드박스에는 데스크톱이 없습니다)
* 키보드/마우스로 OS와 인터랙션

질문이나 빌드 실패 메시지가 나오면 그대로 채팅에 붙여넣어 주세요. 로그를 보고 어디서 막혔는지 분석해서 수정할 수 있습니다.

---

## 부록 B — 디렉터리 정리

이 가이드와 함께 추가된 파일들:

```
bxos/
├── README.utf8.md                  # README EUC-KR → UTF-8 사본
├── SETUP-MAC.md                    # 이 문서
├── run-qemu.sh                     # QEMU 실행 래퍼
├── build-mac.sh                    # 2단계 (Wine) 진입점
├── build-modern.sh                 # 3단계 (NASM + gcc + Python) 진입점
├── build/                          # 빌드 산출물 (gitignore 추천)
│   └── modern/
└── tools/
    ├── shim/                       # build-mac.sh 가 자동 생성 (copy/del 흉내)
    ├── wine-wrappers/              # 2단계용 wine 래퍼들
    │   ├── _run.sh
    │   └── install-symlinks.sh
    └── modern/                     # 3단계용 모듈
        ├── README.md
        ├── nas2nasm.py             # nask → NASM 패치
        ├── hrbify.py               # 32B HRB 헤더 + 0x1B JMP 패치
        ├── makefont.py             # hankaku.txt → 폰트 바이너리
        ├── mkfat12.py              # FAT12 플로피 이미지 생성
        ├── linker-bootpack.lds     # GNU ld 링커 스크립트
        ├── startup_kernel.s        # 커널 진입점 + 32B 헤더 자리
        └── Makefile.modern         # GNU make 빌드 정의
```

원본 트리(`harib27f/`, `z_tools/`, `z_osabin/`, `z_new_o`, `z_new_w`)는 전혀 건드리지 않았습니다. 모두 add-only.
