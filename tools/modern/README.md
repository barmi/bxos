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
단계별 확인이 필요하면 `./build-modern.sh ipl09.bin`, `./build-modern.sh bootpack.elf`
처럼 짧은 타겟 이름을 넘기면 됩니다.

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
| `modern_libc.c` | 커널에서 필요한 `sprintf`/문자열 함수의 freestanding 구현 |
| `linker-bootpack.lds` | 커널 링커 스크립트 (0x0부터 .text 배치) |
| `startup_kernel.s` | 커널 진입점 (32B 헤더 자리 + HariStartup → HariMain) |
| `Makefile.modern` | 위 모두 연결한 GNU make 빌드 정의 |

## 빌드 흐름

```
ipl09.nas      ─ nas2nasm ─ nasm -f bin ─►  ipl09.bin    (boot sector)
asmhead.nas    ─ nas2nasm ─ nasm -f bin ─►  asmhead.bin
naskfunc.nas   ─ nas2nasm ─ nasm -f elf32 ─►  naskfunc.o
kernel *.c + modern_libc.c ─ i686-elf-gcc ─►  *.o
hankaku.txt    ─ makefont.py ─►  hankaku.bin ─ objcopy ─►  hankaku.o
startup_kernel.s ─ nasm -f elf32 ─►  startup_kernel.o    (.text.startup, 32B + HariStartup)

  *.o … ─ ld -T linker-bootpack.lds ─►  bootpack.elf
        ─ objcopy -O binary ─►  bootpack.bin
        ─ hrbify.py ─►  bootpack.hrb          (32B HRB 헤더, 0x1B = JMP HariStartup)

asmhead.bin + bootpack.hrb  ─ cat ─►  haribote.sys
ipl09.bin + haribote.sys + 기존 앱/데이터 파일들 ─ mkfat12.py ─►  haribote.img
```

## 검증된 부분 / 검증되지 않은 부분

Apple Silicon Mac(`i686-elf-gcc`, NASM, Python 3.13)에서 검증한 것:

* `nas2nasm.py` — `ipl09.nas` 의 `RESB 0x7dfe-$` 를 NASM 이 받는 `TIMES 0x1fe-($-$$) db 0` 로 변환.
* NASM — `ipl09.bin`, `asmhead.bin`, `naskfunc.o` 생성.
* `i686-elf-gcc`/`i686-elf-ld` — 커널 C 오브젝트와 `bootpack.elf` 링크.
* `hrbify.py` — 32B HRB 헤더 + `HariStartup` 점프 패치 생성.
* `mkfat12.py` — `build/modern/haribote.img` 생성(39 files).
* `makefont.py` — 4096B 폰트 바이너리 생성, 알려진 글리프 비트맵 일치.

아직 검증하지 않은 것:

* QEMU 부팅 후 실제 동작.

가능성이 있는 트러블 포인트:

1. **QEMU 부팅 후 런타임 동작** — 이미지 생성까지는 통과했지만, 화면 표시/앱 실행은 별도 확인이 필요합니다.
2. **bootpack 의 메모리 레이아웃** — 원본 obj2bim 은 stack/malloc/mmarea 를 정밀하게 배치하지만, 현대 빌드는 linker script + HRB 헤더 생성으로 재현합니다. 부팅 후 비정상 동작 시 이 부분을 먼저 의심합니다.
3. **modern_libc 범위** — 커널이 현재 쓰는 `sprintf`/문자열 함수만 구현했습니다. 새 코드가 더 많은 libc 함수를 쓰면 `modern_libc.c` 에 추가해야 합니다.

## 트러블슈팅 시나리오

다음 명령으로 단계별로 끊어 진행하면 어디서 실패하는지 명확합니다.

```bash
# 0. 도구 확인
./build-modern.sh info

# 1. 어셈블만 (NASM)
./build-modern.sh ipl09.bin
./build-modern.sh asmhead.bin
./build-modern.sh naskfunc.o

# 2. C 컴파일
./build-modern.sh bootpack.o

# 3. 링크
./build-modern.sh bootpack.elf

# 4. flat 바이너리 + HRB 변환
./build-modern.sh bootpack.hrb

# 5. 합치기
./build-modern.sh haribote.sys

# 6. 디스크 이미지
./build-modern.sh haribote.img

# 7. 부팅
qemu-system-i386 -m 32 -fda build/modern/haribote.img -boot a
```

각 단계 결과의 size/file 명령 결과를 원본 `harib27f/haribote/*.bin/.hrb/.img` 와 비교하면 문제 위치 빠르게 좁힐 수 있습니다.
