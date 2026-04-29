# work3 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work3.md](work3.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work3.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

work1 (쓰기 가능 FS + OS/앱 분리 빌드) → work2 (서브디렉터리 / cwd / path) 의 후속이다. 직전 작업의 산출물은 [_doc/work2.md](work2.md), [_doc/work2-handoff.md](work2-handoff.md), [_doc/storage.md](storage.md) 에 정리되어 있다.

---

## 1. 한 줄 요약

현재 폰트 시스템(`nihongo.fnt`, langmode 0/1/2)에 **한글 폰트 (`hangul.fnt`) 와 두 개의 새 langmode — `3 = EUC-KR`, `4 = UTF-8`** 을 추가해, 한글 텍스트를 두 인코딩 모두 콘솔/HE2 앱이 출력할 수 있게 한다. 일본어 지원은 그대로 유지.

## 2. 현재 위치 (2026-04-29 기준)

- **Phase 0 (결정 / 인터페이스 확정) 완료**:
  - 결정 표 잠금 (UTF-8 한글영역 U+AC00..U+D7A3 포함 확정).
  - 폰트 소스 라이선스 확정 — GNU Unifont 의 SIL OFL 1.1 부분 채택. [NOTICE](../harib27f/hangul/NOTICE) 작성.
  - 메모리 슬롯 `0x0fe0` 예약 — grep 으로 미사용 검증, [bootpack.h](../harib27f/haribote/bootpack.h) 상단에 메모리 맵 주석 블록 추가 (0x0fe0/0fe4/0fe8/0fec/0ff0).
  - `struct TASK` 의 `langbyte2` 자리 확정 — `langmode, langbyte1, app_type` 다음 1바이트 자리. NASM/외부 offset 참조 없음. 실제 필드 추가는 Phase 4 코드와 함께.
  - `langmode 3 = EUC-KR`, `langmode 4 = UTF-8` 의미 고정. 둘 다 U+AC00..U+D7A3 한글만, 그 외는 hankaku fallback.
- **Phase 1 (폰트 빌드 도구 + 매핑 테이블) 완료**:
  - [tools/modern/makehangulfont.py](../tools/modern/makehangulfont.py) 신설 — Unifont BDF + hankaku.bin 입력 → `hangul.fnt` + `euckr_map.h` 결정론적 생성. EUC-KR 매핑은 Python 표준 `euc_kr` 코덱 경유 (외부 매핑 파일 의존 없음).
  - [harib27f/hangul/unifont-hangul.bdf](../harib27f/hangul/unifont-hangul.bdf) — Unifont 15.1.05 의 U+AC00..U+D7A3 추출본 (1.9MB, 11172 음절).
  - [harib27f/hangul/LICENSE.fonts](../harib27f/hangul/LICENSE.fonts) — SIL OFL 1.1 본문 + Unifont 저작권.
  - [harib27f/hangul/hangul.fnt](../harib27f/hangul/hangul.fnt) — 생성된 폰트 (361600 B = 4096 ASCII + 11172×32 한글).
  - [tools/modern/euckr_map.h](../tools/modern/euckr_map.h) — 생성된 매핑 헤더 (8836 entries, 2350 valid Hangul mappings).
  - 검증: U+AC00 ("가") 글리프 BDF 와 byte-identical, EUC-KR 0xB0A1→0xAC00 / 0xC8FE→0xD79D / 0xA1A1→0xFFFF 모두 확인, ASCII 영역이 hankaku.bin 동일, 두 번 실행 결과 md5 동일.
- **Phase 2 진입 대기** — bootpack.c 의 `hangul.fnt` 로드 + `0x0fe0` 슬롯 + CMake/Makefile.modern 통합.

## 3. 확정된 핵심 결정 (재확인용)

[work3.md §2](work3.md) 표가 정본. 요약:

- **지원 인코딩**: EUC-KR (KS X 1001 Wansung) **+ UTF-8** 둘 다.
- **새 langmode**: `3` = EUC-KR, `4` = UTF-8. 0/1/2 보존. 두 mode 모두 같은 한글 폰트 풀 공유.
- **폰트 인덱싱 모델**: **Unicode syllable index** (`cp - 0xAC00`, 0..11171). 인코딩 중립.
- **EUC-KR → Unicode 매핑**: 빌드 시 자동 생성된 커널 내장 `g_euckr_to_uhs[94*94]` (16-bit, ≈17.7KB).
- **새 폰트 파일**: `harib27f/hangul/hangul.fnt` (361600 bytes = 4096 ASCII + 11172×32 Hangul). 부팅 FDD (haribote.img) 에 탑재.
- **메모리 슬롯**: `0x0fe0` 에 `hangul` 폰트 시작 주소 4바이트. 0 이면 미적재.
- **TASK 상태 확장**: `langbyte1` 그대로 + `langbyte2` 신설 (UTF-8 3바이트 디코딩용).
- **Fallback 정책**: `hangul.fnt` 미적재 시 `langmode 3`/`4` 거부. EUC-KR 매핑 누락 / UTF-8 비-한글 codepoint / 잘못된 시퀀스 → hankaku fallback.
- **콘솔 init 기본 langmode**: 변경 없음 (nihongo 가 있으면 1, 없으면 0). 사용자가 명시적으로 `langmode 3`/`4` 호출.
- **검증 자산**: `hangul.euc` (EUC-KR), `hangul.utf8` (UTF-8), `khello.he2` (UTF-8), `chklang` 의 mode 3/4 분기.
- **빌드 도구**: `tools/modern/makehangulfont.py` (BDF → `hangul.fnt` + `tools/modern/euckr_map.h`). 결과는 source-controlled.
- **글리프 소스 후보**: GNU Unifont (BMP 전체, OFL/GPL dual) 1차, 또는 Neo둥근모 + 누락 보충.
- **한글 키보드 입력 / IME / Hanja / Jamo 조합 / UTF-8 비-한글** — 모두 범위 밖.

## 4. Phase 한눈에 보기

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 결정 / 인터페이스 | 0.5d | 본 문서 + work3.md, NOTICE, bootpack.h 메모리 맵 주석 (☑ 완료) |
| 1. 폰트 빌드 도구 + 매핑 | 2d | `makehangulfont.py`, `unifont-hangul.bdf`, `hangul.fnt`, `euckr_map.h`, `LICENSE.fonts` (☑ 완료) |
| 2. 커널 폰트 로딩 | 1d | bootpack.c 의 `hangul.fnt` 로드 + `0x0fe0` 슬롯, `g_euckr_to_uhs` include, CMake/Makefile.modern 통합 |
| 3. `langmode 3` (EUC-KR) 렌더링 | 1d | graphic.c 의 EUC-KR 분기 (Unicode 매핑 경유), `cmd_langmode`/`cons_putchar` 확장 |
| 4. `langmode 4` (UTF-8) 렌더링 | 1.5d | TASK 의 `langbyte2` 추가, graphic.c 의 3바이트 UTF-8 디코더 + 한글 영역 추출 |
| 5. 검증 자산 + 콘솔 통합 | 1d | `hangul.euc`, `hangul.utf8`, `khello/khello.he2`, chklang mode-3/4 분기, build wiring |
| 6. 호스트 도구 / 결정론 검증 | 0.5d | bxos_fat.py 회귀, `hangul.fnt` byte-identical 재현 확인 |
| 7. 문서 / 마무리 | 0.5d | BXOS-COMMANDS.md / storage.md / README.utf8.md / SETUP-MAC.md 갱신 |

총 **7~8 작업일**. Phase 2/3/4 가 순차 의존, Phase 5/6 는 4 끝나면 병행 가능.

## 5. 코드 길잡이

| 영역 | 주 변경 파일 |
|---|---|
| 폰트 빌드 도구 | [tools/modern/makehangulfont.py](../tools/modern/makehangulfont.py) (신규), [tools/modern/euckr_map.h](../tools/modern/euckr_map.h) (생성, 신규), [harib27f/hangul/](../harib27f/hangul/) (신규 디렉터리) |
| 커널 부트/폰트 로딩 | [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) (`file_search`/`file_loadfile2` 두 번째 호출 + `g_euckr_to_uhs` 정의), [harib27f/haribote/bootpack.h](../harib27f/haribote/bootpack.h) (메모리 맵 주석 + `langbyte2` 필드) |
| 폰트 렌더링 | [harib27f/haribote/graphic.c](../harib27f/haribote/graphic.c) (`putfonts8_asc` 의 langmode==3 / ==4 분기) |
| 콘솔 / langmode 명령 | [harib27f/haribote/console.c](../harib27f/haribote/console.c) (`cmd_langmode`, `cons_putchar`) |
| 검증 앱 | [harib27f/chklang/chklang.c](../harib27f/chklang/chklang.c) (mode 3/4 분기), [harib27f/khello/khello.c](../harib27f/khello/) (신규 HE2 앱, UTF-8) |
| 빌드 | [CMakeLists.txt](../CMakeLists.txt) (`BXOS_KERNEL_IMG_FILES`, `BXOS_DATA_IMG_FILES`, `BXOS_HE2_APPS_*`), [tools/modern/Makefile.modern](../tools/modern/Makefile.modern), [harib27f/Makefile](../harib27f/Makefile) |
| 문서 | [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [_doc/storage.md](storage.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) |

## 6. 빠른 빌드/실행 치트시트 (work2 와 동일)

```bash
# 첫 설정 (한 번)
cmake -S . -B build/cmake

# 전체 빌드 (kernel-img + data-img)
cmake --build build/cmake

# 부팅 (build/cmake/data.img 가 있으면 자동으로 -hda 부착)
./run-qemu.sh
```

QEMU 콘솔에서 한글 표시 워크플로우:

```
> langmode 3              # EUC-KR
> chklang
> type hangul.euc
> langmode 4              # UTF-8
> chklang
> type hangul.utf8
> khello                  # UTF-8 인사말
> langmode 1              # 일본어 복귀
```

## 7. 함정으로 미리 알아둘 것

work3.md §5 가 정본. 강조:

- **0x0fe0 슬롯 충돌 재확인** — asmhead.nas / ipl09.nas / `BOOTINFO` 와 안 겹치는지 (현재 grep 결과 OK).
- **`langbyte2` 추가가 NASM/외부 offset 참조에 영향 없는지** 확인.
- **DBCS/UTF-8 상태 누수** — `langmode` 변경 시 `langbyte1 = langbyte2 = 0` 초기화. 1/2/3/4 모두.
- **EUC-KR 매핑 누락 영역** — `0xFFFF` 마커, hankaku fallback. graphic.c 가 OOB read 안 하게.
- **UTF-8 안전성** — overlong, surrogate (U+D800..U+DFFF), 4바이트 이상 시퀀스, 잘못된 continuation 모두 reset 후 fallback. 무한 상태 누적 금지.
- **U+AC00 영역 밖** — UTF-8 모드에서 한자/히라가나/라틴 확장은 hankaku fallback (tofu 아님).
- **ASCII 영역 일관성** — `hangul.fnt[0..4095]` = nihongo `[0..4095]` = `hankaku[]`. bootpack.c 에서 강제 통일.
- **폰트 라이선스** — Unifont 의 OFL 부분만 임베드. `harib27f/hangul/LICENSE.fonts` 에 고지. README/SETUP-MAC 에 한 줄.
- **결정론적 빌드** — BDF 직접 변환만 (TTF 라스터 회피).
- **DBCS/UTF-8 절단** — 콘솔 wrap 이 multi-byte 시퀀스 중간을 자르면 깨짐 (기존 일본어와 동일 한계, 문서화).
- **회귀 — 일본어** — langmode 1/2 코드 변경 최소 (langbyte2 초기화 한 줄만).
- **`g_euckr_to_uhs` rodata 17.7KB** — 커널 binary 에 추가, .rodata 배치 확인.

## 8. 작업하지 말아야 할 것

- UTF-8 의 비-한글 codepoint 렌더 / 인코딩 변환 syscall — 후속.
- 한글 키보드 / IME / Jamo 조합 (U+1100 블록) — 후속.
- KS X 1001 한자 / CP949 확장 음절 — 후속.
- 콘솔 prompt / 시스템 메시지 한글화 — 별도 i18n 작업.
- `langmode` 영구화 (재부팅 후에도 mode 3/4) — per-task 휘발성 유지.
- nihongo 폰트 / langmode 1/2 코드 변경 (langbyte2 초기화 한 줄 외) — 회귀 위험.

## 9. 시작 명령

다음 PR/세션 단위 제안:

> "Phase 2 진입: 커널 부트시 `hangul.fnt` 로딩. [bootpack.c](../harib27f/haribote/bootpack.c) 의 `nihongo.fnt` 로딩 직후에 동일 패턴으로 `file_search('hangul.fnt')` + `file_loadfile2`, ASCII 영역(0..4095) 은 `hankaku[]` 로 강제 덮어쓰기, 결과 포인터를 `*((int *) 0x0fe0)` 에 박는다. 미적재 시 0. 빌드 통합: [CMakeLists.txt](../CMakeLists.txt) `BXOS_KERNEL_IMG_FILES` 에 `harib27f/hangul/hangul.fnt` 추가, [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) 도 동일. `cmake --build build/cmake` 통과 + QEMU 부팅 회귀 0 확인."

이후 Phase 3 (EUC-KR 렌더) → Phase 4 (UTF-8 디코더) → Phase 5 (검증 자산) → Phase 6 (호스트 도구) → Phase 7 (문서) 순서로 진행.
