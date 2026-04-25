# tools/modern — 툴체인 현대화 (Wine 의존 제거 시도)

Wine 없이 macOS/Linux 네이티브로 빌드하기 위한 보조 스크립트 모음입니다.
`z_tools/*.exe` 중 **단순한 파일 포맷 변환기**들은 Python으로 충분히 대체할 수 있어
여기에 작성해 두었습니다. 어셈블러/컴파일러/링커처럼 본격적인 툴은 외부의
NASM·GCC·기존 커뮤니티 포팅을 사용하도록 안내합니다.

## 포함된 대체 도구

| Python 스크립트 | 대체 대상 (`z_tools/`) | 비고 |
|---|---|---|
| `makefont.py` | `makefont.exe` | hankaku.txt → 4096B 폰트 바이너리 |
| `mkfat12.py` | `edimg.exe` (harib27f용) | 1.44MB FAT12 플로피 from-scratch 생성 |

## 외부 도구로 대체 / 자체 작성 필요

| 원본 도구 | 권장 대체 | 메모 |
|---|---|---|
| `nask.exe` | NASM (`brew install nasm`) | 문법이 거의 같으나 약간의 차이 존재. `*.nas` 일부 수정 필요. 커뮤니티 포팅: [HariboteOS GitHub](https://github.com/HariboteOS) 참고 |
| `cc1.exe` (gcc 1.x) | `i686-elf-gcc` 크로스 컴파일러 | `brew install i686-elf-gcc` (없으면 `brew install x86_64-elf-gcc` 후 `-m32`) |
| `gas2nask.exe` | 불필요 | 현대 gcc + ld 로 직접 ELF→flat 바이너리 처리 |
| `obj2bim.exe` | 자체 작성 또는 ld + linker script | OSASK BIM 포맷은 책 부록에 형식 설명 있음 |
| `bim2hrb.exe` | 자체 작성 | HRB 헤더 32바이트 + flat code/data |
| `bin2obj.exe` | `objcopy -I binary -O elf32-i386 -B i386` | binutils 의 표준 기능 |
| `aska.exe`, `naskcnv0.exe` | 미사용/대체 안 됨 | OSASK 전용. harib27f 에서는 미사용 경로. |

## 빌드 흐름 (현대 도구 사용 시)

```
*.c   ─ i686-elf-gcc -m32 -ffreestanding -O2 -c ─►  *.o (ELF)
*.nas ─ nasm -f elf32 ─►  *.o (ELF)
hankaku.txt ─ makefont.py ─►  hankaku.bin
hankaku.bin ─ objcopy ─►  hankaku.o
*.o … ─ i686-elf-ld -T link.ld ─►  bootpack.elf
bootpack.elf ─ objcopy -O binary ─►  bootpack.bin
ipl09.bin + asmhead.bin + bootpack.bin (HRB 헤더 추가) ─ cat ─►  haribote.sys
ipl09.bin + haribote.sys + 앱들 ─ mkfat12.py ─►  haribote.img
```

## 현 상태

- [x] `makefont.py` 동작 확인 (4096B 폰트 출력 일치)
- [x] `mkfat12.py` 동작 확인 (`file` 명령 시그니처 원본과 동일)
- [ ] `Makefile.modern` — 위 흐름의 GNU make 화 (작성 예정)
- [ ] `obj2bim`/`bim2hrb` 대체 — 책 부록 또는 [HariboteOS](https://github.com/HariboteOS) 포팅 참조 후 작성

## 검증 방법

```bash
# 폰트 변환 검증
python3 tools/modern/makefont.py harib27f/haribote/hankaku.txt /tmp/hankaku.bin
ls -la /tmp/hankaku.bin   # 4096바이트여야 함

# FAT12 이미지 생성
python3 tools/modern/mkfat12.py \
  --boot harib27f/haribote/ipl09.bin \
  --out  /tmp/my.img \
  harib27f/haribote/haribote.sys harib27f/a/a.hrb
file /tmp/my.img          # FAT (12 bit) 라벨 HARIBOTEOS 확인

# QEMU 부팅
qemu-system-i386 -m 32 -fda /tmp/my.img -boot a
```
