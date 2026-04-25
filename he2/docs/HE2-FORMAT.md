# HE2 — Haribote Executable v2

BxOS의 새로운 실행 파일 포맷 (`.he2`).
기존 HRB 의 32B 헤더 + flat-binary 구조를 단순화하면서, ELF/PE의 좋은
아이디어(섹션 분리, 명시적 BSS, 명시적 stack/heap 요청)를 가져온 것.

## 설계 목표

1. **빌드가 단순할 것** — 표준 i686-elf-gcc + ld + objcopy + 작은 Python
   스크립트만으로 만들어진다. nask / obj2bim / bim2hrb 같은 전용 툴 불필요.
2. **로더가 단순할 것** — 커널 측 로더는 `cmd_app` 한 함수로 30줄 정도.
3. **C API 친화** — 어셈블리 레벨에서만 알 수 있는 매직 (HRB의 `[CS:0x20]`
   같은 트릭) 없이, 모든 런타임 정보는 정의된 헤더 필드와 링커 심볼로
   읽을 수 있다.
4. **앱 소스 무수정** — 기존 `apilib.h` API 시그니처는 그대로 유지.
   재컴파일만 하면 된다.

## 파일 레이아웃

```
+-------------------+ offset 0
| HE2 header (32B)  |
+-------------------+ offset 0x20
| .text             |  코드
| .rodata           |  read-only data
| .data             |  initial data
+-------------------+ offset = image_size
| (file 끝)         |
+-------------------+
```

`image_size` = file size. BSS / heap / stack 은 파일에 들어가지 않고 로드
시 커널이 0으로 채워서 확보한다.

## 헤더 (32 bytes, little-endian)

| Offset | Size | Field         | Description                                   |
|--------|------|---------------|-----------------------------------------------|
| 0x00   | 4    | `magic`       | `"HE2\0"` (`0x00 0x32 0x45 0x48`)             |
| 0x04   | 2    | `version`     | 1                                             |
| 0x06   | 2    | `header_size` | 32                                            |
| 0x08   | 4    | `entry_off`   | 진입점 오프셋 (file/DS 둘 다 동일, ds:0 기준) |
| 0x0C   | 4    | `image_size`  | 파일 전체 크기 (= text+rodata+data 끝)        |
| 0x10   | 4    | `bss_size`    | 0으로 채울 BSS 바이트 수 (image 뒤에 배치)    |
| 0x14   | 4    | `stack_size`  | 요청 스택 크기                                |
| 0x18   | 4    | `heap_size`   | 요청 힙 크기 (api_initmalloc 영역으로 사용)   |
| 0x1C   | 4    | `flags`       | subsystem flags                               |

`flags & 0x03` 은 실행 subsystem 이다.

| Value | Meaning   |
|-------|-----------|
| 0     | Console   |
| 1     | Window    |

## 메모리 레이아웃 (로드 후)

커널은 `image_size + bss_size + heap_size + stack_size` 바이트짜리
연속 영역을 4K 정렬로 할당하고, 같은 base/limit 의 LDT entry 두 개
(코드용 ER, 데이터용 RW) 를 만든다.

```
DS offset
+-----------------------------+ 0
|  HE2 header                 |
|  .text + .rodata + .data    |  ← file image
+-----------------------------+ image_size
|  .bss (zero)                |
+-----------------------------+ image_size + bss_size  = _bxos_heap_start
|  heap                       |
+-----------------------------+ +heap_size              = _bxos_heap_end
|  stack                      |
+-----------------------------+ +stack_size             = ESP 초기값
```

진입 시:
* `CS:EIP = code_seg : entry_off`
* `DS = ES = FS = GS = SS = data_seg`
* `ESP = total_size` (스택 top, 빈 상태로 시작)
* 인자 없음. `_start` 가 `HariMain()` 을 호출하고, 끝나면 `api_end()` 로
  복귀한다.

## API 호출 규약

기존 HRB와 동일한 **`INT 0x40`** 디스패치를 그대로 사용한다.
* `EDX` = function number (1..27)
* `EAX/EBX/ECX/ESI/EDI/EBP` = 인자 (함수별)
* 반환값은 `EAX`

차이점은 단 하나: **포인터 인자가 DS-relative 가 아니라 그냥 C
포인터** 라는 점뿐. 어차피 DS base = 할당된 segment base 이므로
`(char *)ebx + ds_base` 가 그대로 유효한 주소를 만든다 — HRB 와 동일.

라이브러리(`libbxos`) 가 모든 syscall 을 GCC inline asm 으로 래핑하므로,
앱 소스에서는 그냥 C 함수처럼 보인다.

## 링커 심볼

`linker-he2.lds` 가 다음 심볼을 export 한다:

| Symbol            | Meaning                                           |
|-------------------|---------------------------------------------------|
| `_he2_image_end`  | 파일 image 의 끝 (= bss 시작)                     |
| `_he2_bss_end`    | bss 의 끝 (= heap 시작)                           |
| `_bxos_heap_start`| `_he2_bss_end` 의 별칭                            |

스택 top 은 런타임에 정해지므로 (`ESP` 초기값) 링커 심볼이 없다.

## 빌드

```cmake
he2_add_app(a SOURCES a.c)
```

내부적으로:
1. `i686-elf-gcc -m32 -ffreestanding -fno-pic -c a.c → a.o`
2. `i686-elf-ld -T linker-he2.lds a.o libbxos.a → a.elf`
3. `i686-elf-objcopy -O binary a.elf a.bin`
4. `python3 he2pack.py --elf a.elf --bin a.bin --out a.he2`

3·4단계는 한 번에. `he2pack.py` 가 `nm a.elf` 로
`_start`, `_he2_image_end`, `_he2_bss_end` 를 읽어 헤더의 `entry_off`,
`image_size`, `bss_size` 를 채운다.
