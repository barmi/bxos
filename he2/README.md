# BxOS HE2 (Haribote Executable v2)

기존 `.hrb` 포맷과 nask 어셈블리 API 를 대체하는 **현대적 사용자 공간**.

* **포맷**: 32B 헤더 + flat code/data. ELF 의 단순화 버전.
  자세한 사양은 [`docs/HE2-FORMAT.md`](docs/HE2-FORMAT.md).
* **API**: `INT 0x40` syscall 을 GCC inline-asm 으로 래핑한 단일
  C 라이브러리 (`libbxos`). 어셈블리 파일은 진입점 `crt0.S` 단 하나.
  기존 27개(edx 1~27) 에 work1~4 에서 file write/delete (28~30),
  cwd (31), 파일관리 (32~39), 윈도우 이벤트/리사이즈 (40~43) 를 추가해
  현재 edx 1~43 을 사용한다. **work5 는 신규 사용자 syscall 0개** —
  Start Menu / taskbar / 시계 / Run / About 은 모두 커널 내부 widget 이고,
  Settings 앱(`SETTINGS.HE2`)은 기존 file syscall(28~30, 32~39)만 써서
  `/SYSTEM/SETTINGS.CFG` 를 read/write 한다. 자세한 표는
  [`docs/HE2-FORMAT.md`](docs/HE2-FORMAT.md#syscall-디스패치-edx).
* **빌드**: i686-elf-gcc + ld + Python — wine, nask, obj2bim, bim2hrb 불필요.
* **호환성**: 앱 소스(`harib27f/<name>/<name>.c`) 는 한 줄도 수정하지 않고
  그대로 재컴파일된다 (`#include "apilib.h"` 가 그대로 동작).

## 디렉터리 구조

```
he2/
├── README.md                  ← (이 파일)
├── docs/
│   └── HE2-FORMAT.md          ← 포맷 사양서
├── libbxos/
│   ├── include/
│   │   ├── he2.h              ← HE2 헤더 struct (kernel/user 공통)
│   │   ├── bxos.h             ← C API 선언
│   │   └── apilib.h           ← legacy 호환 헤더 (=> bxos.h)
│   └── src/
│       ├── crt0.S             ← .he2_header + _start trampoline
│       └── syscall.c          ← INT 0x40 inline-asm 래퍼 27개
├── tools/
│   ├── linker-he2.lds         ← GNU ld 스크립트
│   └── he2pack.py             ← ELF → .he2 변환 + 검증
└── cmake/
    └── HE2App.cmake           ← bxos_libbxos() / he2_add_app() 매크로
```

## 빠른 사용

루트 `CMakeLists.txt` 가 이미 hooked. macOS 기준:

```bash
brew install cmake nasm i686-elf-gcc i686-elf-binutils qemu
cmake -S . -B build/cmake
cmake --build build/cmake -j           # haribote.img 안에 .he2 들 함께 패키징
cmake --build build/cmake --target run # QEMU 부팅
```

콘솔에서 `a` 입력 → `A.HE2` 가 우선 발견되어 실행. `.hrb` 도 fallback 으로
계속 동작한다.

## 새 앱을 추가하려면

`harib27f/<name>/<name>.c` 가 있으면 루트 CMakeLists 의
`BXOS_HE2_APPS_BASIC` 리스트에 이름만 추가하면 된다. 별도 디렉터리에
새 앱을 두고 싶다면:

```cmake
he2_add_app(myapp
    SOURCES myapp.c util.c
    DIR     ${CMAKE_SOURCE_DIR}/he2/apps/myapp
    STACK   32768
    HEAP    2097152)
```

## 새 syscall 을 추가하려면

1. 커널 측: `harib27f/haribote/console.c` 의 `hrb_api()` 에 `else if (edx == NN)`
   분기 추가.
2. 사용자 측: `he2/libbxos/include/bxos.h` 에 prototype, `he2/libbxos/src/syscall.c`
   에 inline-asm 정의. (한 함수당 5~15 줄.)

별도 빌드 스텝 변경 불필요 — CMake 가 의존성 자동 추적.

## 왜 HE2 인가

| 항목                | HRB                                  | HE2                              |
|---------------------|--------------------------------------|----------------------------------|
| 헤더 magic          | `"Hari"` at offset 4                 | `"HE2\0"` at offset 0            |
| 헤더 안의 magic 위치| 가운데 (재배치 위험)                 | 맨 앞 (POSIX `file` 와 호환)     |
| Entry              | 항상 `0x1B` (E9 JMP rel32 트릭)      | 명시적 `entry_off` 필드          |
| 진입 트램폴린      | 27 NASM 파일 + obj2bim/bim2hrb       | `crt0.S` 1 개                    |
| Syscall 래퍼       | 27개 NASM 파일 + golib00.exe         | `syscall.c` 1 개 (inline asm, edx 1~43) |
| `[CS:0x20]` 같은 트릭| 있음 (`api_initmalloc` 의 malloc 영역)| 없음. 모두 링커 심볼 + 헤더 필드 |
| 빌드 의존성        | Wine(`obj2bim.exe`,`bim2hrb.exe`)    | i686-elf-gcc + GNU ld + python3  |
| 압축               | OSASKCMP                             | (현재 단계) 비압축               |
