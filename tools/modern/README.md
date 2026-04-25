# tools/modern — Wine 의존을 제거한 현대 빌드

`z_tools/*.exe` 를 거치지 않고 NASM + i686-elf-gcc + Python 만으로 BxOS 커널을
빌드합니다. 앱 `.hrb` 들은 OSASKCMP 압축 변환이 비현실적이라 기존 빌드본을 그대로
디스크 이미지에 포함합니다(원본 `harib27f/<app>/*.hrb` 가 이미 빌드된 상태).

## 진입점

```bash
# 워크스페이스 루트에서
./build-modern.sh           # 전체 (build/modern/haribote.img 생성)
./build-modern.sh kernel    # 커널만
./build-modern.sh info      # 도구/경로 점검
./build-modern.sh clean
```

빌드 산출물은 모두 `build/modern/` 아래에 들어갑니다. 원본 트리는 안 건드립니다.

## 사전 설치 (macOS Apple Silicon)

```bash
brew install nasm qemu

# i686-elf-gcc 가 brew 에 있으면 ↓ 가 우선
brew install i686-elf-gcc i686-elf-binutils

# 또는 폴백 (Makefile.modern 가 자동 감지):
brew install x86_64-elf-gcc x86_64-elf-binutils
```

`brew` 가 i686-elf-* 를 못 찾으면 Makefile 이 자동으로 `x86_64-elf-*` 로 폴백하고
`-m32` 를 추가합니다. 둘 다 없으면 호스트 `gcc` 도 시도하지만 macOS Apple Silicon
의 시스템 clang/gcc 는 i386 코드 생성이 안 되니 사실상 elf 크로스 컴파일러 설치가 필수입니다.

## 도구 / 산출물

| 파일 | 역할 |
|---|---|
| `nas2nasm.py` | nask `*.nas` 를 NASM 호환으로 패치 (`[FORMAT]` / `[INSTRSET]` / `[FILE]` 처리) |
| `hrbify.py` | flat binary → 32B HRB 헤더 + 0x1B JMP 패치 → `.hrb` |
| `mkfat12.py` | 1.44MB FAT12 플로피 이미지 from-scratch 생성 (edimg 대체) |
| `makefont.py` | hankaku.txt → 4096B 폰트 바이너리 (makefont.exe 대체) |
| `linker-bootpack.lds` | 커널 링커 스크립트 (0x0부터 .text 배치) |
| `startup_kernel.s` | 커널 진입점 (32B 헤더 자리 + HariStartup → HariMain) |
| `Makefile.modern` | 위 모두 연결한 GNU make 빌드 정의 |

## 빌드 흐름

```
ipl09.nas      ─ nas2nasm ─ nasm -f bin ─►  ipl09.bin    (boot sector)
asmhead.nas    ─ nas2nasm ─ nasm -f bin ─►  asmhead.bin
naskfunc.nas   ─ nas2nasm ─ nasm -f elf32 ─►  naskfunc.o
*.c            ─ i686-elf-gcc ─►  *.o
hankaku.txt    ─ makefont.py ─►  hankaku.bin ─ objcopy ─►  hankaku.o
startup_kernel.s ─ nasm -f elf32 ─►  startup_kernel.o    (.text.startup, 32B + HariStartup)

  *.o … ─ ld -T linker-bootpack.lds ─►  bootpack.elf
        ─ objcopy -O binary ─►  bootpack.bin
        ─ hrbify.py ─►  bootpack.hrb          (32B HRB 헤더, 0x1B = JMP HariStartup)

asmhead.bin + bootpack.hrb  ─ cat ─►  haribote.sys
ipl09.bin + haribote.sys + 기존 앱 *.hrb 들 ─ mkfat12.py ─►  haribote.img
```

## 검증된 부분 / 검증되지 않은 부분

샌드박스(aarch64 Linux, 네트워크 allowlist + non-root)에서 검증한 것:

* `nas2nasm.py` — `naskfunc.nas`/`asmhead.nas`/`ipl09.nas` 변환 시 nask 디렉티브가 NASM 호환 형태로 정확히 치환됨.
* `hrbify.py` — 32B 헤더 바이트 레이아웃이 a.hrb 와 동일한 형태로 생성 (`Hari` 매직, ESP, `0x1B = 0xE9`, rel32 entry).
* `mkfat12.py` — 부트섹터 512B 가 원본 `haribote.img` 와 byte-identical, FAT12 메타 시그니처 일치.
* `makefont.py` — 4096B 폰트 바이너리 생성, 알려진 글리프 비트맵 일치.

샌드박스에서 검증 못 한 것 (사용자 Mac에서 처음 돌아갈 부분):

* NASM 으로 `*.nasm.nas` 를 실제 어셈블 — 샌드박스에 NASM 설치 불가.
* `i686-elf-gcc` 로 `*.c` 컴파일 — 샌드박스 aarch64 + 크로스 컴파일러 미설치.
* `ld -T linker-bootpack.lds` 링크 — 위와 동일 이유.
* QEMU 부팅 후 실제 동작.

가능성이 있는 트러블 포인트:

1. **NASM 문법 미세 차이** — `nas2nasm.py` 가 처리하는 3가지 외에도 `nask` 가 이상하게 인코딩하는 명령이 있을 수 있어요. 특히 `JMP DWORD 2*8:0x1b` 같은 far jump 는 nask/NASM 모두 받지만 인코딩 방식이 다를 수 있습니다. 어셈블 에러가 나면 그 줄을 보고 수정.
2. **GCC C 코드 호환성** — bxos 의 C 는 K&R 스타일과 ANSI 가 섞여 있고, `int -> ptr` 캐스트 등에서 modern gcc 는 경고/에러를 냅니다. `Makefile.modern` 에 `-Wall` 만 켜고 `-Werror` 는 안 켰지만, 정 안 되면 `-Wno-int-conversion -Wno-implicit-int -Wno-pointer-sign` 등 추가.
3. **링커 심볼 underscore 규칙** — `i686-elf-gcc` 는 보통 underscore 를 안 붙이는데, 옛 nask 코드는 `_io_hlt` 처럼 `_` 가 붙은 이름을 export 합니다. 두 가지 길:
   * `nas2nasm.py` 단계에서 GLOBAL/EXTERN 의 `_` 를 떼는 추가 처리.
   * 또는 `bootpack.h` 의 함수 선언 앞에 `__attribute__((noinline))` 등으로 우회 — 근본 해결은 첫 번째.
   * 우선 그대로 빌드해 보고 unresolved symbol 에러가 뜨면 그 때 패치.
4. **`hankaku` 심볼 이름** — Makefile 에서 `_hankaku` 로 export 하도록 `objcopy --redefine-sym _binary_hankaku_bin_start=_hankaku` 설정. C 쪽에서 `extern char hankaku[];` 로 보는지 `extern char _hankaku[];` 로 보는지 확인 필요.
5. **bootpack 의 메모리 레이아웃** — 원본 obj2bim 은 stack/malloc/mmarea 를 정밀하게 배치하지만, 우리는 단일 flat .text 로만 묶음. 커널이 stack 영역을 자기 마음대로 쓰지 않고 ESP 만 의존한다면 OK. 부팅 후 비정상 동작 시 의심.

## 트러블슈팅 시나리오

다음 명령으로 단계별로 끊어 진행하면 어디서 실패하는지 명확합니다.

```bash
# 0. 도구 확인
./build-modern.sh info

# 1. 어셈블만 (NASM)
make -f tools/modern/Makefile.modern build/modern/ipl09.bin
make -f tools/modern/Makefile.modern build/modern/asmhead.bin
make -f tools/modern/Makefile.modern build/modern/naskfunc.o

# 2. C 컴파일
make -f tools/modern/Makefile.modern build/modern/bootpack.o

# 3. 링크
make -f tools/modern/Makefile.modern build/modern/bootpack.elf

# 4. flat 바이너리 + HRB 변환
make -f tools/modern/Makefile.modern build/modern/bootpack.hrb

# 5. 합치기
make -f tools/modern/Makefile.modern build/modern/haribote.sys

# 6. 디스크 이미지
make -f tools/modern/Makefile.modern build/modern/haribote.img

# 7. 부팅
qemu-system-i386 -m 32 -fda build/modern/haribote.img -boot a
```

각 단계 결과의 size/file 명령 결과를 원본 `harib27f/haribote/*.bin/.hrb/.img` 와 비교하면 문제 위치 빠르게 좁힐 수 있습니다.
