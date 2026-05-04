# work6 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work6.md](work6.md) 작업을 새 세션에서 이어받기 위한 단일
진입점이다. 먼저 work6.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음
행동을 잡으면 된다.

work1 (쓰기 가능 FAT16) → work2 (서브디렉터리/cwd) → work3 (한글 출력) →
work4 (파일 탐색기) → work5 (Start Menu / Settings / taskbar) 다음 작업이다.

---

## 1. 한 줄 요약

BxOS GUI 의 hot path 를 측정 가능하게 만들고, 컴포지터·폰트·부분 갱신·신규
syscall 4종 (`api_blit_rect`/`api_text_run`/`api_invalidate_rect`/`api_dirty_flush`)
을 통해 mouse drag / 텍스트 출력 / 윈도우 갱신 응답성을 **수치로 2배 이상**
향상시킨다. 측정·검증은 PIT 기반 `bench` 인프라 위에서.

## 2. 현재 위치 (2026-05-04 기준)

- work6 은 **Phase 0~7 구현 완료 + Phase 1~7 측정 완료 + Phase 8 문서 완료** 단계다.
  남은 일: 사용자 측 QEMU full smoke (work5 의 모든 동작 + 측정 시나리오 5개) 만.
- Phase 1~7 모든 측정값 ([_doc/work6-bench.md](work6-bench.md)) 기록 완료. 누적 결과:
  - 워크로드 안정 시나리오 (S1/S2/S5, manual measurement P1→P3) 평균 -27%.
  - P5 → P7 자동 측정 비교: S4 의 syscall 빈도는 99% 절감 (401 → 1) ✓, 그러나 절대
    Σ는 -7.5% 만 — refresh 면적 비용이 syscall 절감을 상당 부분 상쇄. 자세한 분석은
    work6-bench.md 의 "Phase 7 핵심" 단락.
- 측정 방법 전환점: P5 부터 `bench scenario` 자동 측정 도입 (수동 5~10 분 → 자동 3.8 초).
- 신규 syscall edx 44~47 (`api_blit_rect` / `api_text_run` / `api_invalidate_rect` /
  `api_dirty_flush`) 모두 커널 + libbxos 양쪽에 구현 + 문서화 완료. `api_point` (11) 은
  deprecated 표기.
- Phase 7 앱 변경:
  - tetris_t.c: `BENCH_MODE = 2` (default) — `api_blit_rect` 1회 경로. cell × 200 → 1 syscall.
  - settings.c: `draw_settings()` → sidebar/panel/status 분리 + `G_dirty` 비트마스크.
  - explorer.c: `on_mouse_move` 가 hover 변동 / drag 시만 redraw_all.
  - tetris.c / lines.c / evtest.c: 변경 없음 (이미 효율적이거나 의도적 즉시 그리기).
- Phase 8 문서:
  - BXOS-COMMANDS.md: `bench` 명령 표 + 신규 syscall 표 + `bench scenario` 절차 추가.
  - README.utf8.md: "성능 / 측정 (work6)" 단락 추가.
  - SETUP-MAC.md: 2.7 절 `bench` 사용법 추가.
  - he2/README.md: edx 1~47 갱신, deprecation 메모.
  - he2/docs/HE2-FORMAT.md: syscall 표 1~47 (Phase 6 시 작성 완료).

## 3. 확정할 핵심 결정

work6.md §2 가 정본이다. 요약:

- **측정 단위**: per-function 은 **RDTSC cycles**, per-scenario (S1~S5) 는 PIT 10ms tick.
  PIT 단독으로는 ㎲ 스케일 hot 함수를 분해하지 못해 RDTSC 도입 (Phase 0 결정 갱신).
- **측정 대상 hot path**: `sheet_refreshmap/sub`, `sheet_slide`, `boxfill8`,
  `putfont8`, `putfonts8_asc`, `taskbar_full_redraw`, `scrollwin_redraw`, `hrb_api`.
- **Quick wins**: `boxfill8 → memset`, `sheet_refreshsub` 1바이트 path 의 run-length
  memcpy, 4바이트 path 의 정렬 보정, `putfonts8_asc_ascii` 신규 entry point.
- **컴포지터 일반화**: refreshmap/sub 의 4바이트 path 를 모든 vx0 정렬에 일반화.
- **폰트 lookup**: `hankaku_fast[256][16][8]` 32KB. 부팅 시 1회 expand.
- **Dirty rect 인프라**: `struct SHEET` 에 `dirty_rect[4]`, `sheet_dirty_add` /
  `sheet_dirty_flush` / `sheet_dirty_flush_all`. HariMain idle 직전에 flush.
- **부분 redraw**: taskbar (start/clock/winlist 분리), scrollwin (append 분리),
  마우스 (같은 좌표 skip).
- **신규 syscall** (edx 44~47): `api_blit_rect`, `api_text_run`, `api_invalidate_rect`,
  `api_dirty_flush`. `api_point` (11) 은 deprecated 표기.
- **App polish**: explorer / tetris / settings / 콘솔 / lines 등 batch 사용 정리.
- **성공 기준**: S1~S5 시나리오 평균 baseline 대비 **2배 이상**, 회귀 0건.

## 4. 측정 시나리오 (work6.md §4)

| ID | 시나리오 | 목적 |
|---|---|---|
| S1 | 부팅 → Ctrl+Esc 토글 10회 → Start 종료까지 elapsed | taskbar / menu redraw |
| S2 | 콘솔 `dir` 10연속 elapsed | scrollwin / 텍스트 출력 |
| S3 | explorer `/` 50개 entry 첫 그리기 elapsed | 앱 측 redraw 종합 |
| S4 | tetris 한 라인 클리어 시점 보드 redraw elapsed | 게임 응답성 / blit_rect |
| S5 | 마우스 1000회 미세 이동의 sheet_slide 누적 ticks | 마우스 hover 비용 |

baseline 은 Phase 1 끝에 [_doc/work6-bench.md](work6-bench.md) 에 기록.
이후 모든 phase 끝에 같은 시나리오 재측정 + 비교 표.

## 5. 자료 구조 / API 변경 (work6 도입)

### 측정 인프라 (Phase 1)

```c
/* harib27f/haribote/bench.h */
struct BENCH_COUNTER {
    const char *name;
    uint32_t calls;
    uint64_t total_cycles;   /* RDTSC 누적 */
    uint64_t max_cycles;     /* RDTSC 최대 */
};

extern int g_bench_enabled;     /* 0 = off, hot path 즉시 return */

void bench_enter(int idx);      /* 내부적으로 rdtsc() 캐시 */
void bench_leave(int idx);
void bench_dump(void);          /* dbg_putstr0 로 출력 */
void bench_reset(void);
void bench_set_enabled(int on);

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}

enum {
    BENCH_REFRESHMAP = 0,
    BENCH_REFRESHSUB,
    BENCH_SHEET_SLIDE,
    BENCH_BOXFILL8,
    BENCH_PUTFONT8,
    BENCH_PUTFONTS8_ASC,
    BENCH_TASKBAR,
    BENCH_SCROLLWIN,
    BENCH_HRB_API,
    BENCH_COUNT
};
```

Per-function counters 는 RDTSC. Scenario S1~S5 elapsed 는 별도로 `timerctl.count`
(PIT 10ms tick) 의 시작/끝 차이를 본다 — `bench` 명령이 두 값 모두 보여줌.

### Dirty rect (Phase 4)

```c
/* bootpack.h SHEET 확장 */
struct SHEET {
    /* 기존 필드 ... */
    int dirty_count;          /* 0..4 */
    short dirty_rect[4][4];   /* x0,y0,x1,y1 */
};

void sheet_dirty_add(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_dirty_flush(struct SHEET *sht);
void sheet_dirty_flush_all(struct SHTCTL *ctl);
```

### 신규 syscall (Phase 6)

| edx | 이름 | 인자 (HE2 wrapper) |
|---|---|---|
| 44 | `api_blit_rect` | `(int win, void *src_buf, int sx, int sy, int sw, int sh, int dx, int dy)` |
| 45 | `api_text_run` | `(int win, int x, int y, int color, const char *text, int len, int defer) → int` |
| 46 | `api_invalidate_rect` | `(int win, int x0, int y0, int x1, int y1)` |
| 47 | `api_dirty_flush` | `(int win)` |

`api_point` (edx 11) 는 deprecated 표기만, 동작 유지.

## 6. Phase 한눈에 보기 (work6.md §3)

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 요구사항 / 인터페이스 ☑ | 0.5d | 측정 시나리오, syscall 번호 (44~47) 잠금 (완료 2026-05-01) |
| 1. 측정 인프라 ☑ | 1.5d | `bench.c/h`, `bench` 콘솔 명령 (구현 완료, baseline 측정 대기) |
| 2. Quick wins ☑ | 1d | rep stosb/movsb memset/memcpy, boxfill8 row memset, refreshsub run-length memcpy, putfonts8_asc_ascii (구현 완료, 측정 대기) |
| 3. 컴포지터 일반화 + 폰트 lookup ☑ | 1.5d | refreshmap unified memset, putfont 256x2 마스크 lookup, branchless 32-bit blit (구현 완료, 측정 대기) |
| 4. Dirty rect 인프라 ☑ | 2d | SHEET dirty_rect[4][4], sheet_dirty_add/flush/flush_all, idle flush, 호환 mode (구현 완료, smoke 대기) |
| 5. Taskbar / scrollwin / 마우스 부분 redraw ☑ | 2d | sheet_slide same-coord skip, scrollwin per-line append/backspace + scrollbar 분리, taskbar tray-only refresh (구현 완료, 측정 대기) |
| 6. App 측 syscall 추가 / 정리 ☑ | 1.5d | edx 44=blit_rect, 45=text_run, 46=invalidate_rect, 47=dirty_flush; libbxos wrapper; HE2-FORMAT 갱신; api_point deprecated; tetris_t 가 batch 사용 (구현 완료, 측정 대기) |
| 7. App side polish ☑ | 1.5d | tetris_t blit_rect mode (default), settings 영역 분리 + dirty bitmask, explorer mouse_move hover-skip (구현 완료, 측정 완료 — S4 -7.5% / refresh 면적이 syscall 절감 일부 상쇄) |
| 8. 회귀 / 문서 ☑ | 1d | BXOS-COMMANDS bench 단락 + 신규 syscall, README / SETUP / he2/README 갱신, work6-bench Phase 1~7 표 + 분석 (남은 일: 사용자 측 QEMU full smoke) |

총 9~11 작업일 예상 (Phase 0~8 구현/측정/문서 완료, 남은 일은 사용자 측 회귀 smoke).

## 7. 코드 길잡이

| 영역 | 볼 파일 |
|---|---|
| 컴포지터 | [harib27f/haribote/sheet.c](../harib27f/haribote/sheet.c) `sheet_refreshmap`/`sub`/`slide` |
| 그래픽 primitive | [harib27f/haribote/graphic.c](../harib27f/haribote/graphic.c) `boxfill8`/`putfont8`/`putfonts8_asc` |
| Taskbar / mouse loop | [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) `taskbar_full_redraw`, HariMain |
| Console / scrollwin | [harib27f/haribote/console.c](../harib27f/haribote/console.c) `scrollwin_redraw`, `hrb_api` |
| 윈도우 그리기 | [harib27f/haribote/window.c](../harib27f/haribote/window.c) `make_window8`, `make_textbox8` |
| 측정 인프라 (신규) | `harib27f/haribote/bench.c`, `bench.h` (Phase 1) |
| HE2 syscall wrapper | [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h), [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) |
| HE2 ABI 문서 | [he2/docs/HE2-FORMAT.md](../he2/docs/HE2-FORMAT.md) syscall 표 (Phase 6 시 갱신) |
| 앱 측 (polish 대상) | [harib27f/explorer/explorer.c](../harib27f/explorer/explorer.c), [harib27f/tetris/tetris.c](../harib27f/tetris/tetris.c), [harib27f/settings/settings.c](../harib27f/settings/settings.c) |

## 8. 빠른 빌드/실행 치트시트

```bash
# Release 빌드 (성능 측정용)
cmake -S . -B build/cmake
cmake --build build/cmake
./run-qemu.sh

# Debug 빌드 (CLion / GDB)
cmake -S . -B build/cmake-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cmake-debug
./run-qemu.sh --debug

# 측정 (Phase 1 후)
> bench on
> dir          ← S2 시나리오
> bench dump   ← debug 창에 표 출력
> bench reset  ← S2 외 영향 제거 후 다른 시나리오
```

호스트 검증:

```bash
fsck_msdos -n build/cmake/data.img
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/
i386-elf-readelf -S build/cmake-debug/bootpack.elf | grep debug_info  # Debug 빌드 확인
```

## 9. 바로 시작할 때 할 일

1. `git status --short` — 작업트리 깨끗한지 확인.
2. ~~Phase 0 잠금~~ — 완료 (2026-05-01). work6.md §3 Phase 0 표가 정본.
3. ~~Phase 1 구현~~ — 완료 (2026-05-01). bench 인프라 + hot path 9곳 + `bench` 콘솔
   명령 + work6-bench.md. 남은 일은 QEMU 안에서 baseline 측정 후 표 채움.
4. ~~Phase 2 구현~~ — 완료 (2026-05-01). `memset`/`memcpy` 를 `rep stosb`/`rep movsb`
   inline asm 으로 (`tools/modern/modern_libc.c`). `boxfill8` 이 row 당 `memset`.
   `sheet_refreshsub` 의 4바이트/1바이트 path 통합 → run-length scan + `memcpy`
   (sheet.c, 80→30줄). `putfonts8_asc_ascii` ascii fast path 신규, langmode==0
   에서 위임. 호스트 검증 통과 (`objdump` 에서 rep stos / rep movs emit 확인).
5. ~~Phase 3 구현~~ — 완료 (2026-05-01). `sheet_refreshmap` 4/1바이트 path 통합
   (opaque → row 통째로 `memset`, transparent → run-length `memset`). `putfont8` 의
   8 ifs/row → 256-entry 마스크 lookup (`g_putfont_mask_lo/hi`, 2KB) + branchless
   32-bit AND/OR. 부팅 시 `putfont_mask_init()` 한 번. 호스트 검증: `_putfont8`
   row 본문에 conditional branch 0건, `_sheet_refreshmap` 가 `call _memset` 사용.
6. ~~Phase 4 구현~~ — 완료 (2026-05-02). `struct SHEET` 에 `short dirty_count`
   + `short dirty_rect[4][4]` 추가 (8 KB). `sheet_dirty_add` (best-cost union 휴리스틱),
   `sheet_dirty_flush`, `sheet_dirty_flush_all` (HariMain idle 직전 호출), `sheet_dirty_pending`.
   `sheet_refresh` 동작 변경 없음 — 호환 mode. 새 dirty 모델은 Phase 5 에서 본격 사용.
   호스트 검증 통과 (objdump 에 4개 신규 symbol).
7. ~~Phase 5~~ — 완료 (2026-05-02). taskbar partial refresh (`taskbar_redraw_clock_only` /
   `_start_only`), scrollwin per-line (`scrollwin_redraw_line` + scrollbar 분리),
   sheet_slide same-coord skip. `bench scenario` 자동 측정 도입. S2 scrollwin avg
   1.4 M → 70 K (-95%).
8. ~~Phase 6~~ — 완료 (2026-05-03). edx 44~47 (api_blit_rect / api_text_run /
   api_invalidate_rect / api_dirty_flush) 커널 + libbxos. tetris_t 가 `USE_PHASE6_BATCH`
   로 invalidate+flush 경로 사용. 측정: S4 +10.6% 회귀 (cell × 200 invalidate_rect
   syscall overhead) — Phase 7 에서 blit_rect 로 수정.
9. ~~Phase 7~~ — 완료 (2026-05-04). tetris_t `BENCH_MODE = 2` 로 `api_blit_rect` 1회 경로
   (28 KB BSS board_buf + winbuf row-blit). settings.c sidebar/panel/status 분리 +
   `G_dirty` 비트마스크. explorer.c `on_mouse_move` hover-skip. 측정: S4 syscall 빈도
   -99.8% 달성, 그러나 refresh 면적 비용으로 절대 Σ -7.5% 만.
10. ~~Phase 8~~ — 문서 완료 (2026-05-04). BXOS-COMMANDS.md / README.utf8.md /
    SETUP-MAC.md / he2/README.md / work6-bench.md / work6.md / 본 handoff 갱신.
    남은 일은 사용자 측 QEMU full smoke (work5 항목 + 측정 시나리오 5개).
11. ~~work6 종료 정리~~ — 완료 (2026-05-04). 효과 없이 오버헤드만 있는 부분 정리:
    (a) HariMain idle 의 `sheet_dirty_flush_all` 호출 제거 — dirty rect 호출처가
    syscall 핸들러뿐이고 syscall 자체가 즉시 자체 flush 라 idle path 의 256-sheet
    순회는 work 0. (b) `taskbar_redraw_start_only` dead code 제거 — 호출처 0.
    (c) tetris.c (실제 게임) 에 board_buf + api_blit_rect 적용 — line-clear 후
    redraw_field 만 cell × 200 boxfilwin → 1 blit_rect (deferred). piece 움직임은
    그대로 (이미 효율적).

## 10. 함정으로 미리 알아둘 것

- **PIT 10ms 분해능** — 짧은 함수 (e.g. `boxfill8` 한 번) 는 0~1 tick. call count 와 누적
  ticks 의 평균을 봐야 의미 있음.
- **`-fno-builtin`** — i686-elf-gcc toolchain 의 freestanding 플래그. `memset`/`memcpy` 가
  inline 안 될 수 있음. Phase 2 에서 [tools/modern/modern_libc.c](../tools/modern/modern_libc.c)
  에 안전한 `rep stosb`/`rep movsb` helper 를 두거나, `-fbuiltin` 일부 허용 검토.
- **Dirty rect 4개 cap** — 5번째 rect 가 들어오면 외접 합집합으로 합쳐 일부 영역을
  과다 refresh. 일반적인 워크로드에서는 영향 작지만 게임처럼 많은 작은 변경에서 문제 가능.
  Phase 5 의 부분 redraw 가 4개 안에 fit 하도록 설계.
- **`sheet_refresh` 호환** — 기존 호출이 즉시 refresh 를 기대. Phase 4 에서 변경하지 말 것.
  새 dirty 모델은 `sheet_dirty_add` 로 분리.
- **HE2 ABI 동결** — 신규 syscall 44~47 추가 후 work6 종료 직전엔 ABI 변경 금지.
  앱 polish 단계에서 syscall spec 변경 욕구가 생기면 Phase 6 까지 모아서.
- **app_point (11)** — 사용 코드 검색 후 단계적 제거 검토 (work7).
- **putfont expansion table 의 시점** — 부팅 매우 초기(`init_palette` 직후 권장) 에 1회.
  이후 read-only 라 lock 불필요.
- **bench 코드 자체** — `bench_enter` 가 `if (!enabled) return;` 첫줄. force-inline 화.
- **explorer 의 splitter 동작** — 부분 redraw 도입 후 splitter drag 시 잔상 가능성.
  Phase 7 의 핵심 회귀 포인트.
- **scrollwin append 의 viewport overflow** — 단순 append 가 마지막 line 인 경우만 빠름.
  스크롤이 필요하면 fallback to full redraw — 정상 동작.

## 11. 작업하지 말아야 할 것 (work6.md §6)

- multi-process / fork / signal.
- VGA 16색 → 256색 / true color.
- network / sound / printer.
- compositor GPU acceleration.
- HE2 → ELF 직접 로드.
- syscall ABI 안정화 / freeze (work7 검토).
- mouse cursor 모양 다양화.
