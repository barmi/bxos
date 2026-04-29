# work3 — 한글(EUC-KR) 폰트 지원 추가

## 1. 배경 / 목표

### 현재 상태 (work2 종료 시점)
- 커널이 부팅 시 [haribote.img](../build/cmake/haribote.img) 의 [nihongo.fnt](../harib27f/nihongo/nihongo.fnt) 를 읽어 폰트 포인터를 `*((int *) 0x0fe8)` 에 박아 둔다. 파일 레이아웃은 `0..4095` = 16×256 hankaku(8×16 ASCII), 그 뒤 `(k*94 + t)*32` 인덱스의 16×16 full-width 글리프 (k=0..46) — 일본어 JIS X 0208 기준.
- 렌더링은 [graphic.c](../harib27f/haribote/graphic.c) 의 `putfonts8_asc()` 가 task 의 `langmode` 로 분기:
  - `langmode == 0`: ASCII (`hankaku`).
  - `langmode == 1`: Shift-JIS DBCS (lead 0x81..0x9f / 0xe0..0xfc → k 매핑, trail 변환).
  - `langmode == 2`: EUC-JP DBCS (lead/trail 둘 다 0xa1..0xfe → k=lead-0xA1, t=trail-0xA1).
- 콘솔 console_task 시작 시 [console.c](../harib27f/haribote/console.c#L79) 에서 `nihongo[4096] != 0xff` 면 `langmode = 1`, 아니면 0 으로 시작. `langmode <0|1|2>` built-in 으로 변경 가능.
- 한국어 글자(EUC-KR / UTF-8 등)를 콘솔에 찍거나 파일을 `type` 하면 일본어 글리프나 깨진 글리프로 보인다 — **한글 글리프가 폰트에 없음**.
- 빌드: [CMakeLists.txt](../CMakeLists.txt#L294) 의 `BXOS_KERNEL_IMG_FILES` 에 `nihongo.fnt` 만 들어 있고, [tools/modern/Makefile.modern](../tools/modern/Makefile.modern#L222) 에도 같은 한 줄.

### 이번 작업의 목표
1. **한글 글리프 추가** — Unicode Hangul Syllables (U+AC00..U+D7A3) 11172자 전체를 16×16 비트맵으로 표시할 수 있게.
2. **`langmode = 3` 도입 (EUC-KR)** — 기존 EUC-JP 분기와 같은 모양의 DBCS 렌더링. 입력 바이트는 EUC-KR Wansung, 내부적으로 Unicode 음절 인덱스로 변환해 폰트 조회.
3. **`langmode = 4` 도입 (UTF-8)** — 3바이트 UTF-8 한글 시퀀스 (lead 0xEA..0xED) 디코딩 후 동일 한글 폰트 버퍼 조회. ASCII (1바이트) 패스스루.
4. **일본어 지원 유지** — `nihongo.fnt` / `langmode 0/1/2` 는 그대로. 한글은 **추가** 모드.
5. **검증 자산** — 한글 샘플 파일 두 개(`hangul.euc` EUC-KR, `hangul.utf` UTF-8) + HE2 검증 앱(`khello.he2`) + `chklang.he2` 의 `langmode==3/4` 분기.
6. **빌드 통합** — 호스트 폰트 (BDF/TTF) 에서 `hangul.fnt` 를 생성하는 도구 + CMake / `Makefile.modern` 에 동일 한 줄로 통합. 내장 EUC-KR → Unicode 매핑 테이블도 같이 생성.

## 2. 설계 결정 사항 (확정 — 2026-04-29)

| 항목 | 결정 | 비고 |
|---|---|---|
| 지원 인코딩 | **EUC-KR (KS X 1001 Wansung) + UTF-8** 둘 다 | EUC-KR 은 기존 DBCS 모델, UTF-8 은 신규 3바이트 디코더. 둘 다 동일한 글리프 풀(Unicode Hangul Syllable index)을 공유. |
| 폰트 인덱싱 모델 | **Unicode syllable index** (`cp - 0xAC00`, 0..11171) | EUC-KR 입력은 내장 매핑 테이블로 Unicode 로 변환 후 같은 인덱스 사용. UTF-8 입력은 codepoint 디코드 후 직접 인덱스. 폰트 파일이 인코딩 중립. |
| Hangul Jamo 조합 | 미지원 (precomposed 음절만) | U+AC00..U+D7A3 11172 개 완성형만. Jamo (U+1100 블록) 조합은 렌더 단에서 처리 안 함. |
| Hanja / KS X 1001 한자 | **이번 범위 밖** | 1차에서는 한글 음절 영역 + ASCII. 그 외 codepoint 는 hankaku fallback (또는 tofu). |
| langmode 번호 | **3 = EUC-KR**, **4 = UTF-8** | 0,1,2 보존. 두 mode 모두 같은 한글 폰트 버퍼를 사용. |
| 폰트 포인터 슬롯 | **0x0fe0** 에 4바이트 (`hangul` 버퍼 시작 주소) | 0x0fe4 (shtctl), 0x0fe8 (nihongo), 0x0fec (fifo) 사용 중. 0x0fe0 은 비어 있음. `bootpack.h` 의 `BOOTINFO` (0x0ff0) 와도 충돌 없음. |
| EUC-KR → Unicode 매핑 | 커널 내장 `static const unsigned short g_euckr_to_uhs[94*94]` (16-bit) ≈ 17.7KB | 빌드 시 `makehangulfont.py` 가 `tools/modern/euckr_map.h` 도 함께 생성, [bootpack.c](../harib27f/haribote/bootpack.c) 가 include. 매핑 미존재 슬롯은 0xFFFF. |
| 폰트 파일명 | **`hangul.fnt`** | nihongo.fnt 와 동일 디렉터리(`harib27f/hangul/`) 에 둠. |
| 폰트 파일 레이아웃 | `0..4095`: 16×256 hankaku(ASCII, nihongo hankaku 와 동일) <br> `4096..`: 32바이트씩 Unicode syllable index 순서 — `font[idx] = hangul + 4096 + idx*32`, idx = 0..11171 | 총 4096 + 32×11172 = 361600 bytes (≈353 KB). 미정의 위치 없음 (전체 음절 11172개 모두 채움). |
| 빈 글리프 표시 | 글리프 누락 음절은 `0xff` (tofu) — 폰트 BDF 에 빠진 음절이 있으면 그 슬롯만 0xff. | KSX 1001 영역 밖이지만 Unicode 영역인 음절은 BDF 글리프가 없을 수 있음 — 그 경우만 tofu. |
| 잘못된 시퀀스 처리 | EUC-KR: 매핑 없는 lead/trail → hankaku fallback (lead/trail 각각 1바이트로 출력). <br> UTF-8: 잘못된 lead/continuation → 디코더 reset, 해당 바이트 hankaku 로 출력. | DBCS 상태 누수 없게 매 호출에서 `langbyte1`/`langbyte2` 정리. |
| 폰트 누락 시 fallback | **`hangul.fnt` 없으면 `langmode 3`/`4` 거부** | 콘솔이 `cmd_langmode` 에서 hangul 포인터 미초기화 시 에러 메시지 출력 후 mode 변경 거부. |
| 한글 음절 글리프 소스 | **Neo둥근모 (Neodgm) 16px BDF** 또는 **GNU Unifont** 의 한글 블록 (U+AC00..U+D7A3) | Neo둥근모는 SIL OFL, Unifont 는 GPL/OFL. 11172 음절 전부 커버되어야 하므로 **Unifont 가 1차 후보** (전체 BMP 커버). 미디어 임베드 시 라이선스 호환 확인. |
| 폰트 변환 도구 | **`tools/modern/makehangulfont.py`** | 입력: BDF (`unifont.bdf`). 출력: `hangul.fnt` + `tools/modern/euckr_map.h`. ASCII 4096바이트는 hankaku.bin 또는 nihongo.fnt 의 0..4095 그대로 복사. 결정론적 — 동일 입력에서 byte-identical 결과. 결과는 source-controlled. |
| 폰트 파일 위치 | **data.img** 에 탑재 | `haribote.img` 는 IPL 이 `CYLS=20` 범위만 선적재하므로 361600B `hangul.fnt` 를 끝까지 읽지 못한다. `bootpack.c` 가 `fs_data_search`/`fs_data_loadfile` 로 ATA/FAT16 경로에서 직접 읽는다. |
| 콘솔 init 기본 langmode | **변경 없음** — nihongo 가 있으면 1, 없으면 0 | 한글이 기본이 되면 기존 일본어 텍스트가 깨진다. 사용자가 `langmode 3`/`4` 을 명시적으로 호출. |
| 콘솔 한글 입력 | **이번 범위 밖** | 키보드는 그대로 ASCII/SJIS 코드만. 한글은 EUC-KR/UTF-8 로 미리 인코딩된 파일을 `type` 하거나 앱이 인코딩된 바이트를 `api_putstr0` 로 출력. |
| TASK DBCS 상태 확장 | `langbyte1` (현재 1바이트) → 그대로 + **`langbyte2`** 신설 | UTF-8 의 3바이트 디코딩에 lead+mid 두 바이트 보관 필요. EUC-KR/SJIS/EUC-JP 는 `langbyte1` 만 사용. |
| 검증 샘플 | `harib27f/hangul.euc` (EUC-KR), `harib27f/hangul.utf` (UTF-8) <br> `harib27f/khello/khello.c` — UTF-8 모드를 가정한 HE2 앱 | data.img 에 같이 넣음. FAT 8.3 이름은 `HANGUL.EUC`, `HANGUL.UTF`. host 에서 `iconv` 로 확인 가능. |
| chklang 확장 | `langmode == 3` 분기 ("한국어 EUC-KR 모드"), `langmode == 4` 분기 ("한국어 UTF-8 모드") 둘 다 추가 | EUC-KR 와 UTF-8 바이트 배열을 각각 정적 상수로. |
| `BXOS-COMMANDS.md` / 문서 | `langmode 3` / `langmode 4`, `khello.he2`, `hangul.euc` / `hangul.utf`, 한글 출력 워크플로우 추가 | work2 와 같은 톤. |

## 3. 작업 단계

체크박스(☐)는 PR 경계를 표시한다.

### Phase 0 — 결정 / 인터페이스 확정 (0.5일)
- ☑ 2장 결정 표 잠금. 본 문서 + work3-handoff.md 에 반영. (UTF-8 추가 요구 반영 끝.)
- ☑ 폰트 소스 라이선스 확정 — **GNU Unifont 의 OFL 1.1 부분** 1차 채택. GPL 전염성 회피 위해 OFL 인용. 대안(Neo둥근모/Galmuri) 도 SIL OFL — Unifont 품질 이슈 시 교체 가능. [harib27f/hangul/NOTICE](../harib27f/hangul/NOTICE) 작성. 정본 OFL 본문(`LICENSE.fonts`) 은 Phase 1 에서 BDF 와 함께 동봉.
- ☑ 새 메모리 슬롯 `0x0fe0` 예약 — grep 결과 미사용 확인 (현재 0x0fe4/8/c, 0x0ff0 만 사용). [bootpack.h](../harib27f/haribote/bootpack.h) 상단에 메모리 맵 주석 블록 추가.
- ☑ `struct TASK` 의 `langbyte2` 자리 확정 — 현재 `unsigned char langmode, langbyte1, app_type;` (3 byte) 다음에 1 byte 추가 시 자연 정렬 유지 (`name[16]` 가 1-byte 정렬). NASM/외부 offset 참조 없음 확인 (TASK 는 C 전용). 실제 필드 추가는 Phase 4 에서 코드와 함께.
- ☑ `langmode 3` (EUC-KR) / `langmode 4` (UTF-8) 의미 고정 — 둘 다 한글 음절(U+AC00..U+D7A3)만, Jamo/Hanja/UTF-8 의 다른 영역은 hankaku fallback. 잘못된 시퀀스도 reset 후 fallback (graphic.c 단에서 보호).

### Phase 1 — 폰트 빌드 도구 + 매핑 테이블 (2일)
**목표**: 호스트에서 BDF 한 개로부터 `hangul.fnt` (Unicode-indexed 11172 음절) + `euckr_map.h` (EUC-KR → Unicode 16-bit 테이블) 를 결정론적으로 생성.

- ☑ [tools/modern/makehangulfont.py](../tools/modern/makehangulfont.py) 신설.
  - 입력: `--bdf <unifont-hangul.bdf>`, `--ascii <hankaku.bin>`, `--out-fnt hangul.fnt`, `--out-map euckr_map.h`.
  - U+AC00..U+D7A3 의 11172 음절을 16×16 비트맵으로 추출 — BDF 의 BBX/BITMAP 직접 해석. 16비트 행 단위 (`>> 8` 왼쪽 / `& 0xFF` 오른쪽) 로 32바이트 글리프, `idx = cp - 0xAC00` 위치에 기록.
  - BDF 누락 음절은 32바이트 `0xFF` 로 채움 (tofu) — Unifont 는 모두 가지므로 정상 입력에서 발생 안 함.
  - ASCII 영역(0..4095): `hankaku.bin` (4096 bytes) 그대로 복사.
  - **EUC-KR → Unicode 매핑** 생성: Python `bytes([lead,trail]).decode('euc_kr')` 로 (lead, trail ∈ 0xA1..0xFE) 96×96 격자 디코드 → U+AC00..U+D7A3 범위만 매핑, 그 외 0xFFFF. 결과를 `static const unsigned short g_euckr_to_uhs[8836]` C 헤더로 직렬화.
- ☑ [harib27f/hangul/](../harib27f/hangul/) 디렉터리에 라이선스 / NOTICE / 원본 BDF 배치.
  - [unifont-hangul.bdf](../harib27f/hangul/unifont-hangul.bdf) — Unifont 15.1.05 의 U+AC00..U+D7A3 추출본 (1.9MB, 11172 음절).
  - [LICENSE.fonts](../harib27f/hangul/LICENSE.fonts) — SIL OFL 1.1 본문 + Unifont 저작권 명시.
  - [NOTICE](../harib27f/hangul/NOTICE) — Phase 0 에서 작성.
- ☑ 결과 [hangul.fnt](../harib27f/hangul/hangul.fnt) (361600 B) + [tools/modern/euckr_map.h](../tools/modern/euckr_map.h) (72 KB) 생성, git 추가.
- ☑ 검증:
  - ☑ U+AC00 ("가") slot 의 32 바이트가 BDF 의 해당 BITMAP 행 16개와 byte-identical (`0000000000043f8400840084008400870104010402040c043004000400040004`).
  - ☑ EUC-KR 매핑: `g_euckr_to_uhs[(0xB0-0xA1)*94 + (0xA1-0xA1)] == 0xAC00`, `[B0A2]==AC01`, `[C8FE]==D79D`, `[A1A1]==0xFFFF`. 총 매핑 수 = 2350 (KS X 1001 한글 표준 일치).
  - ☑ `wc -c hangul.fnt` = 361600 (= 4096 + 32×11172). 0..4095 영역이 `build/cmake/hankaku.bin` 과 동일.
  - ☑ 같은 입력으로 두 번 실행 → `hangul.fnt`, `euckr_map.h` 둘 다 md5 동일 (결정론).

### Phase 2 — 커널 폰트 로딩 (1일)
**목표**: 부팅 후 `data.img` 에서 `hangul.fnt` 를 읽어 별도 버퍼에 두고 0x0fe0 에 포인터 박기. 없으면 NULL.

- ☑ [bootpack.c](../harib27f/haribote/bootpack.c#L111) 의 nihongo 로딩 직후 `hangul.fnt` 로딩 블록 추가:
  - `fs_data_search("hangul.fnt")` / `fs_data_loadfile()` — 데이터 디스크 root.
  - 있으면 `fs_data_loadfile` 로 읽고 ASCII 영역(0..4095) 은 `hankaku[]` 로 강제 덮어쓰기 (nihongo 와 같은 안전장치).
  - 없으면 `*((int *) 0x0fe0) = 0` — fallback 없음(폰트 미적재).
- ☑ [bootpack.h](../harib27f/haribote/bootpack.h) 메모리 맵 주석 갱신: "0x0fe0 = hangul font ptr (0 if absent)".
- ☑ 빌드 통합:
  - ☑ [CMakeLists.txt](../CMakeLists.txt#L315) 의 `BXOS_DATA_IMG_FILES` 에 `${BXOS_HARIB}/hangul/hangul.fnt` 추가. `hangul.fnt` 는 source-controlled 로 두고 CMake 는 의존성으로 잡음.
  - ☑ [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) 도 `haribote.img` + `data.img` 분리 구성으로 보정하고 `hangul.fnt` 는 data.img 로 이동.
- ☑ 검증:
  - ☑ `cmake --build build/cmake --target kernel` / 전체 빌드 통과.
  - ☑ QEMU headless smoke (`QEMU_EXTRA='-display none -no-reboot' ./run-qemu.sh --no-data`, 5초 유지 후 종료) 통과.
  - ☑ `bxos_fat.py ls build/cmake/data.img:/` 결과에 `HANGUL.FNT` 보임.

### Phase 3 — `langmode 3` (EUC-KR) 렌더링 (1일)
**목표**: `putfonts8_asc()` 에 EUC-KR 분기 추가. 기존 1/2 분기 패턴 그대로, 단 글리프 인덱스는 Unicode syllable 변환 경유.

- ☑ [graphic.c](../harib27f/haribote/graphic.c#L107) `putfonts8_asc` 에 `langmode == 3` 분기 추가:
  - `char *hangul = (char *) *((int *) 0x0fe0);`
  - `hangul == 0` 이면 hankaku 만 출력 (안전 fallback).
  - lead 0xA1..0xFE → `langbyte1` 보관. 다음 trail 0xA1..0xFE 도착 시 `idx = g_euckr_to_uhs[(lead-0xA1)*94 + (trail-0xA1)]`. `idx == 0xFFFF` 면 lead/trail 각각 hankaku 로 fallback. 정상이면 `font = hangul + 4096 + (idx-0xAC00)*32`. left/right 16바이트씩 putfont8.
  - 잘못된 lead/trail 범위 → hankaku fallback.
- ☑ [graphic.c](../harib27f/haribote/graphic.c) 와 [console.c](../harib27f/haribote/console.c) 에 `tools/modern/euckr_map.h` include — Phase 1 에서 생성한 `g_euckr_to_uhs` 사용.
- ☑ [console.c](../harib27f/haribote/console.c#L361) `cons_putchar` / 스크롤 콘솔 렌더러의 DBCS 폭 처리에 mode 3 추가.
- ☑ [console.c](../harib27f/haribote/console.c#L1210) `cmd_langmode`:
  - 허용 mode 를 `<= 4` 로 확장.
  - mode 3 변경 시 `*((int *) 0x0fe0) == 0` 이면 `"hangul font not loaded.\n"` 출력 후 거부.
  - mode 변경 시 `task->langbyte1 = task->langbyte2 = 0` 으로 초기화 (DBCS/UTF-8 중간 상태 잔존 방지) — 1/2 mode 도 같이 적용.
- ☑ 검증:
  - ☑ QEMU 콘솔에서 `langmode 3` 후 `type hangul.euc` 가 한글 정상 출력.
  - ◐ 일본어 회귀는 이번 정리에서 범위 밖으로 두기로 결정. `langmode 1/2` 코드는 유지하되 스크롤 콘솔의 일본어 렌더링은 후속에서 복구.
  - ☑ 매핑 없는 EUC-KR 슬롯은 `0xFFFF` 검사 후 hankaku fallback.

### Phase 4 — `langmode 4` (UTF-8) 렌더링 (1.5일)
**목표**: 3바이트 UTF-8 한글 디코더 추가. ASCII (1바이트) 패스스루.

- ☑ [bootpack.h](../harib27f/haribote/bootpack.h#L247) `struct TASK` 에 `unsigned char langbyte2` 추가 (langbyte1 옆). 초기값 0.
- ☑ [bootpack.c](../harib27f/haribote/bootpack.c) 의 task 초기화 / [console.c](../harib27f/haribote/console.c) 의 console_task 시작부에 `langbyte2 = 0` 명시.
- ☑ [graphic.c](../harib27f/haribote/graphic.c#L107) `putfonts8_asc` 에 `langmode == 4` 분기 추가:
  - state machine: `langbyte1 == 0` 이면 첫 바이트 검사 — `< 0x80` 은 ASCII 패스스루 (hankaku 출력), `0xEA..0xED` 면 `langbyte1 = byte`, 그 외(다른 lead) 는 hankaku fallback.
  - `langbyte1 != 0 && langbyte2 == 0` 이면 mid 바이트 — `0x80..0xBF` 면 `langbyte2 = byte`, 그 외면 reset 후 fallback.
  - `langbyte1 != 0 && langbyte2 != 0` 이면 trail 바이트 — `0x80..0xBF` 면 codepoint 계산:
    - `cp = ((langbyte1 & 0x0F) << 12) | ((langbyte2 & 0x3F) << 6) | (byte & 0x3F)`.
    - `0xAC00 <= cp <= 0xD7A3` 이면 `font = hangul + 4096 + (cp - 0xAC00)*32`, left/right putfont8.
    - 범위 밖이면 hankaku fallback.
    - `langbyte1 = langbyte2 = 0`.
- ☑ `cmd_langmode`: 허용 mode 를 `<= 4` 로 확장. mode 4 진입 시 hangul 포인터 검사.
- ☑ `cons_putchar` / 스크롤 콘솔 렌더러의 UTF-8 진행 상태와 폭 처리 추가.
- ☑ 검증:
  - ☑ QEMU 콘솔에서 `langmode 4` → `type hangul.utf` 가 한글 정상 출력.
  - ☑ ASCII (영문/숫자) 가 mode 4 에서도 정상.
  - ☑ mode 3 ↔ mode 4 토글 후 양쪽 모두 정상 (langbyte1/langbyte2 초기화 확인).

### Phase 5 — 검증 자산 및 콘솔 통합 (1일)
- ☑ [harib27f/hangul.euc](../harib27f/hangul.euc) — EUC-KR 인코딩.
- ☑ [harib27f/hangul.utf](../harib27f/hangul.utf) — 같은 내용의 UTF-8 인코딩. FAT 8.3 이름도 `HANGUL.UTF`.
- ☑ [harib27f/chklang/chklang.c](../harib27f/chklang/chklang.c) — `langmode == 3` / `langmode == 4` 분기 추가.
- ☑ [harib27f/khello/khello.c](../harib27f/khello/) — 새 HE2 앱. UTF-8 으로 인코딩된 정적 한글 문자열을 `bx_putstr0` 로 출력 후 종료. (사용 시 콘솔이 `langmode 4` 여야 정상 표시.)
- ☑ 빌드 통합:
  - ☑ [CMakeLists.txt](../CMakeLists.txt) `BXOS_HE2_APPS_BASIC` 에 `khello` 추가, `BXOS_DATA_IMG_FILES` 에 `hangul.euc` / `hangul.utf` / `hangul/hangul.fnt` 추가.
  - ☑ [tools/modern/Makefile.modern](../tools/modern/Makefile.modern) 에 `data.img` 타겟과 한글 파일 목록 반영.
- ☑ 검증 시나리오 (QEMU 대화형):
  - ☑ `langmode 3` 후 `type hangul.euc` → 한글 정상.
  - ☑ `langmode 4` 후 `type hangul.utf` → 한글 정상.
  - ☑ `langmode 4` 후 `khello` → UTF-8 인사말 정상.
  - ☑ `langmode 5` 같은 잘못된 값은 기존 에러 그대로.

### Phase 6 — 호스트 도구 / 폰트 재생성 검증 (0.5일)
- ☑ [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py) — 변경 없음 (text 파일/바이너리 그대로 다룸). EUC-KR / UTF-8 텍스트가 image 왕복에서 보존되는지 확인.
- ☑ `makehangulfont.py` 를 다시 돌려도 byte-identical `hangul.fnt` + `euckr_map.h` 가 나오는지 (결정론) 확인.
- ☑ `hangul.euc` / `hangul.utf` 의 바이트가 data.img 에서 그대로인지 `bxos_fat.py` 추출 후 `cmp` 로 검증.

### Phase 7 — 문서 / 마무리 (0.5일)
- ☑ [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) — `langmode 3` (EUC-KR) / `langmode 4` (UTF-8), `khello.he2`, `hangul.euc` / `hangul.utf`, 한글 출력 워크플로우 두 가지 추가.
- ☑ [_doc/storage.md](storage.md) — data.img 의 `hangul.fnt`, 메모리 맵 0x0fe0 슬롯, TASK 의 `langbyte2` 필드 언급.
- ☑ [README.utf8.md](../README.utf8.md) / [SETUP-MAC.md](../SETUP-MAC.md) — 한글 표시 한 단락 (EUC-KR + UTF-8 두 워크플로우) + HE2 앱 수 갱신.
- ☑ work3.md / work3-handoff.md 체크박스 닫음.

## 4. 마일스톤 / 검증 시나리오

| 끝난 시점 | 검증 |
|---|---|
| Phase 1 | `makehangulfont.py` 가 BDF 입력으로부터 결정론적 `hangul.fnt` (361600B) + `euckr_map.h` 생성. U+AC00 ("가") 슬롯이 BDF 비트와 일치. EUC-KR `0xB0A1` → `0xAC00` 매핑 성립. |
| Phase 2 | 커널이 부팅 후 data.img 에서 `hangul.fnt` 를 로드, 0x0fe0 에 포인터 보유. 미적재 빌드에서도 부팅 정상 (포인터 0). |
| Phase 3 | QEMU `langmode 3` 후 `type hangul.euc` 정상. EUC-KR 매핑 누락 슬롯은 fallback. 일본어 `langmode 1/2` 의 스크롤 콘솔 렌더링은 후속 작업으로 분리. |
| Phase 4 | QEMU `langmode 4` 후 `type hangul.utf` 정상. ASCII 패스스루 OK. mode 3 ↔ 4 토글 OK. |
| Phase 5 | `chklang` / `khello.he2` / `type` 가 mode 3, 4 양쪽에서 의도대로. |
| Phase 6 | host↔image 텍스트 인코딩 보존. 폰트 재생성 byte-identical. |
| Phase 7 | 문서가 두 mode + 두 파일 + 신규 앱을 반영. 신규 진입자가 EUC-KR / UTF-8 두 워크플로우를 한 페이지로 따라갈 수 있음. |

## 5. 위험 요소 / 함정

- **메모리 슬롯 충돌** — `0x0fe0` 이 정말 비어 있는지 grep 으로 재확인. asmhead.nas / ipl09.nas / bootpack.h `BOOTINFO` 와 겹치면 안 됨. (현재 grep 결과 0x0fe4/0x0fe8/0x0fec/0x0ff0 만 사용.)
- **TASK 구조체 변경** — `langbyte2` 추가 시 패딩/정렬이 바뀌어 다른 모듈에서 캐시한 offset 이 깨질 수 있음. `bootpack.h` 의 `struct TASK` 만이 정본인지 확인 (NASM offset 등 외부 참조 없음 확인).
- **EUC-KR 미배정 영역** — KS X 1001 의 한글 영역도 일부 슬롯이 미배정. `g_euckr_to_uhs` 에 `0xFFFF` 로 채워두고 graphic.c 에서 명시적 검사 → hankaku fallback. lead/trail 모두 0xA1..0xFE 범위 밖이면 같은 처리.
- **UTF-8 디코더 안전성** — overlong encoding (예: ASCII 를 2바이트로 표현), surrogate codepoint (U+D800..U+DFFF), 5/6 바이트 시퀀스 (RFC 3629 위반) 를 모두 거부. continuation 바이트가 `0x80..0xBF` 범위가 아니면 즉시 reset. 무한 상태 누적 금지.
- **U+AC00 영역 밖 codepoint** — UTF-8 모드에서 ASCII 외의 비-한글 codepoint (히라가나/한자 등) 가 들어오면 hankaku fallback. tofu 가 아니라 깨진 ASCII 처럼 보이지만 안전.
- **ASCII 영역 일관성** — `hangul.fnt` 의 0..4095 가 nihongo.fnt 의 같은 영역과 다르면 langmode 토글 시 ASCII 글리프가 바뀌어 보임. bootpack.c 의 nihongo 로딩 패턴 (`for (i=0..4096) buf[i] = hankaku[i]`) 을 hangul 에도 그대로 적용 — 런타임에 `hankaku[]` 로 강제 덮어쓰기.
- **DBCS / UTF-8 상태 누수** — `langmode` 변경 시 `task->langbyte1`/`langbyte2` 가 0 이 아니면 다음 모드의 첫 바이트와 결합되어 깨진 글리프. `cmd_langmode` 에서 항상 둘 다 0 으로 초기화.
- **폰트 라이선스** — GNU Unifont 는 OFL/GPL dual; 임베드 시 GPL 전염성 검토 후 OFL 부분 인용 + NOTICE 동봉. Neo둥근모는 SIL OFL. `harib27f/hangul/LICENSE.fonts` 에 출처/조건 명시. README / SETUP-MAC 에 한 줄 라이선스 고지.
- **BDF → 16×16 변환 정확성** — BDF 폰트의 BBX 가 16×16 가 아니면 (Unifont 는 일부 글리프가 16×16 가 아닐 수 있음) padding/clipping 처리 필요. baseline 정렬 확인.
- **결정론적 빌드** — TTF 경로를 쓰면 freetype 버전 차이로 1픽셀씩 흔들림. **BDF 직접 사용** 으로 통일.
- **부팅 FDD 선적재 한계** — `haribote.img` 는 1.44MB FAT12 이지만 IPL 이 `CYLS=20` 범위만 메모리에 올린다. `hangul.fnt` 는 data.img 로 이동하고 ATA/FAT16 경로로 읽는다.
- **DBCS / UTF-8 절단** — 콘솔 wrap 이 한 줄 끝에서 multi-byte 시퀀스 중간을 자르면 깨질 수 있음. 기존 일본어 모드와 동일 한계로 문서화 (수정은 후속).
- **회귀 — 일본어** — `langmode 1/2` 코드 경로는 변경 최소. mode 3/4 분기는 새로 추가만, 기존 분기는 `langbyte2 = 0` 초기화 한 줄만 추가.
- **EUC-KR 매핑 테이블 크기** — 94×94×2 = 17672 bytes 가 BSS/rodata 에 추가됨. 커널 binary 에는 영향 적지만 .rodata 위치 확인.

## 6. 범위 외 (이번 작업에서 안 하는 것)

- UTF-8 의 비-한글 codepoint (히라가나/한자/라틴 확장) — hankaku fallback 만.
- 한글 키보드 입력 / IME / 조합 (Jamo → 음절).
- Hangul Jamo 블록 (U+1100..U+11FF) 의 조합형 렌더.
- KS X 1001 한자 영역, KS X 1002 확장, CP949 확장 음절.
- UTF-8 의 BOM, normalization (NFC/NFD), surrogate pair.
- 가변폭 글리프, 안티앨리어싱, 그레이스케일 폰트.
- 한글 정렬 / 자모 분해 / locale-aware 비교.
- 콘솔 prompt / dir 출력 등 시스템 텍스트의 한글화 — 영어/일본어 그대로.
- 인코딩 변환 syscall (EUC-KR ↔ UTF-8) — 앱 레벨에서 처리.
- 폰트 동적 로딩/언로딩, 다중 폰트 슬롯 일반화.
- `langmode` 의 영구화 (재부팅 후 자동 mode 3/4) — per-task 휘발성 유지.

## 7. 예상 일정

총 **7~8 작업일** (한 사람 풀타임 기준). Phase 1 (폰트 도구 + 매핑 테이블) 이 가장 손이 많이 가고, Phase 4 (UTF-8 디코더) 는 상태기 정확성 + 시각 검증 시간 필요. Phase 2/3/4 는 순차 의존, Phase 5/6 는 4 끝나면 병행 가능.
