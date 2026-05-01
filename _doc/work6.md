# work6 — GUI 효율화 / 부분 갱신 / 측정 인프라 / 신규 syscall 정리

## 1. 배경 / 목표

### 현재 상태 (work5 종료 시점)

- 컴포지터 ([sheet.c](../harib27f/haribote/sheet.c)) 의 `sheet_refreshmap` / `sheet_refreshsub`
  가 픽셀 단위 byte loop. 4-byte fast path 가 있지만 `vx0 & 3 == 0` + 폭이 4의 배수일 때만.
- `boxfill8`([graphic.c:65](../harib27f/haribote/graphic.c:65)) 도 byte loop. `memset` /
  `rep stosb` 미사용.
- `putfont8` 은 글자당 16행 × 8 비트 분기. 8x16 비트맵을 하나씩 검사.
- `putfonts8_asc` 는 ascii 한 글자마다 `task_now()` + langmode 분기.
- 거의 모든 redraw 가 윈도우/영역 전체 단위.
  - `taskbar_full_redraw` 가 hover 한 픽셀 변화에도 28px × scrnx 전체 다시 그림.
  - `scrollwin_redraw` 가 한 줄 추가 / 커서 깜박임에도 viewport 전체 fill 후 모든 line redraw.
  - explorer 의 `redraw_all` 도 전체 client area 재그림.
- 마우스 sheet 는 매 인터럽트마다 `sheet_slide` → `sheet_refreshmap` + `sheet_refreshsub`
  두 번 호출 (옛 위치 + 새 위치). 마우스 100Hz 기준 1초당 200회.
- HE2 syscall 1~43 은 모두 동기 single-rect refresh 모델. batch 는 `bit0=1` deferred
  플래그 한 가지뿐.
- 프로파일링 도구 / 벤치마크 / 측정 인프라가 **전혀 없음**. 변경 효과 검증이 주관적.

### 이번 작업의 목표

**한 줄로**: BxOS GUI 의 hot path 를 측정 가능한 형태로 만들고, 가장 비용 큰 구간부터
순차로 다듬어 mouse drag / 텍스트 출력 / 윈도우 갱신 응답성을 **수치로 2~5배** 향상시킨다.
필요하면 syscall ABI 도 정리·확장한다.

세부 목표:

1. **측정 인프라**: 커널 안에 PIT 기반 누적 타이머 / 카운터 + debug 창 출력. 모든 hot path 에 측정.
2. **컴포지터 fast path 일반화**: `boxfill8 → memset`, `sheet_refreshsub` 의 1바이트 path 에 run-length memcpy, 4-byte path 정렬 보정.
3. **텍스트 fast path**: `putfont8` 8x16 미리 펼친 lookup 테이블, ascii 전용 `putfonts8_asc_ascii`.
4. **부분 갱신 (dirty rect)**: 커널 측 `SHEET` 에 dirty rect 누적, refresh deferral. taskbar / scrollwin / app 모두 부분 redraw.
5. **마우스 cursor blit 경량화**: 별도 fast path. 위치 변동 미세하면 skip.
6. **신규 syscall** (편의 + 성능):
   - `api_blit_rect` — 사용자 buffer 의 rect → window 의 rect (이미지 보기 / 게임 더블 버퍼링).
   - `api_text_run` — 한 syscall 로 여러 글자 + 색 + ASCII fast path.
   - `api_invalidate_rect` — 그리지 않고 dirty 만 표시 (batch 후 마지막에 refresh 한 번).
   - `api_dirty_flush` — 누적된 dirty rect 합쳐 한 번에 refresh.
7. **App side polish**: explorer / tetris / settings / lines / evtest 가 새 ABI 또는 deferred flag 를 일관되게 쓰게 정리.
8. **회귀 검증**: 모든 phase 끝에 측정값을 함께 기록 (Phase 1 인프라 위에서).

## 2. 설계 결정 사항 (계획안 — 2026-05-01)

| 항목 | 결정 | 비고 |
|---|---|---|
| 측정 인프라 단위 | **per-function: RDTSC cycle**, **per-scenario: PIT 10ms tick**. counter 1개 = `{ enter_count, total_cycles, max_cycles }`. | PIT 만으로는 boxfill8/putfont8 같은 ㎲ 스케일 함수 측정 불가. RDTSC 는 단일 CPU/단일 task 환경이라 변동 작음. QEMU TCG 도 RDTSC 에뮬. |
| 측정 출력 | `bench` 콘솔 명령 + `dbg_putstr0` ("Start → Programs → Debug" 에서 확인). | 별도 GUI 위젯은 work7 이후. cycle 값은 `/ scenario_cycles * 100` 으로 % 표시도 같이. |
| 측정 토글 | 기본 OFF (오버헤드 0). `bench on` 으로 켬. | per-counter `enabled` 플래그. RDTSC 자체는 1 cycle 정도라 무시. |
| boxfill8 구현 | `memset` per-row. 1바이트 pattern, x86 의 `rep stosb` 가 백엔드. | i686-elf-gcc 가 `__builtin_memset` 으로 자동 변환. |
| refreshsub 1-byte path | run-length: 같은 sid 의 연속 픽셀을 찾아 `memcpy`. 분기는 run 시작 시 한 번. | col_inv != -1 sheet 도 동일하게 적용. |
| refreshsub 4-byte path | `vx0 & 3 != 0` 이어도 처음/끝 끝수만 1바이트로 처리하고 가운데는 4바이트 비교/복사. | menu sheet 가 200px 폭이라 정렬 보정 필요. |
| putfont8 lookup table | `hankaku_fast[256][16][8]` = 32KB, 부팅 시 1회 expand. 글자당 row 마다 8 byte 중 set 비트만 store. | 한글 폰트는 그대로 (글자수 × 32 byte 너무 큼). |
| putfonts8_asc fast path | langmode==0 에서 `task_now()` 한 번, ascii 전용 inner loop. | 신규 entry point `putfonts8_asc_ascii` 추가. |
| dirty rect 모델 | sheet 마다 `dirty_rect[4]` (max 4 사각형 합집합). 더 많아지면 union 으로 합침. | per-window 단일 rect 에서 시작해 점진 확장. |
| dirty flush 시점 | (a) HariMain 의 idle 진입 직전 (b) 명시적 `api_dirty_flush` (c) 일정 시간 (10ms) 경과. | (a) 가 기본, (b) 가 즉시 강제. |
| 기존 `sheet_refresh` API | 내부적으로 `sheet_dirty_add` + 즉시 flush 로 구현. 외부 호환 유지. | App-side 코드 무수정 동작. |
| taskbar 부분 redraw | dirty rect 기반. Start / clock / winlist 각자 별도 영역. clock 1초 모드도 tray 영역만. | hover 추적도 영역별. |
| scrollwin 분리 | `scrollwin_redraw_all` (현재) + `scrollwin_redraw_lines(start, n)` + `scrollwin_redraw_scrollbar` + `scrollwin_append_line`. | console 이 append 로 변경. |
| 마우스 batching | 이미 fifo 비우는 시점에 last 적용. 추가로 같은 정수 좌표면 skip. | sheet_slide 안에서도 idempotent check. |
| 새 syscall 번호 | edx 44~47 — 44=blit_rect, 45=text_run, 46=invalidate_rect, 47=dirty_flush. | work6 안에서 한 번에 추가. |
| 신규 syscall ABI 호환 | `api_point`(11) 는 deprecated 표기. 동작은 유지 (HE2 앱 호환). | `api_blit_rect` 가 대체. |
| 사용자 syscall 제거 | 0개 (호환 유지). | api_point 만 deprecated 표기. |
| App side 변경 | explorer / settings / tetris 만 의무. lines / evtest / sosu 는 polish. | 회귀 위험 분리. |
| 측정 비교 baseline | work5 ea345d6 (Phase 5 종료) 와 work6 각 phase 종료의 같은 시나리오 반복. | "explorer 디렉터리 100개 listing 시간" 등 정해 둔 시나리오 5개. |
| QEMU vs native | 측정은 QEMU TCG 위에서. Native 차이는 다를 수 있으나 추세는 동일. | 모든 측정 같은 QEMU 옵션. |
| 성공 기준 | 정해진 시나리오 5개에서 baseline 대비 **2배 이상** 빨라짐 (전체 하모닉 평균). 그리고 회귀 0건. | Phase 7 종료 시 평가. |

## 3. 작업 단계

각 Phase 끝의 ☐ 는 PR/커밋 단위 자연 경계. baseline 측정은 Phase 1 끝에서.

### Phase 0 — 요구사항 / 인터페이스 확정 (0.5일) — ☑ 완료 (2026-05-01)
- ☑ 본 문서 + [work6-handoff.md](work6-handoff.md) 를 정본으로 두고 MVP 범위 잠금.
- ☑ §2 결정 표 잠금. 변경 시 두 파일 동시 갱신.
- ☑ 측정 시나리오 5개 확정:
  - **S1**: 부팅 → Start 메뉴 5번 토글 (Ctrl+Esc x10) 까지 elapsed.
  - **S2**: 콘솔에서 `dir` × 10번 elapsed (스크롤 포함).
  - **S3**: explorer `/` 디렉터리 (전체 50개 entry) 트리/리스트 그리기 elapsed.
  - **S4**: tetris 1라인 클리어 (보드 전체 한 번 redraw) elapsed.
  - **S5**: 마우스 cursor 화면 가로지르기 (1000 step) total ticks.
- ☑ 신규 syscall edx 번호 잠금 (44~47). HE2-FORMAT.md 갱신은 Phase 6.
- ☑ 범위 외 명시 (§6).

**Phase 0 추가 검증 노트 (2026-05-01)**
- syscall 충돌: `grep -rn "edx == 44|45|46|47"` 결과 0건. work5 종료 시점에서 1~43 만
  사용 — 44~47 안전하게 잠금.
- 파일명 충돌: `harib27f/haribote/bench.c` / `bench.h` 미존재. `_doc/work6-bench.md` 미존재.
- 콘솔 명령 충돌: 기존 명령 (mem/cls/dir/cd/pwd/mkdir/rmdir/task/taskmgr/disk/touch/rm/cp/mv/echo/mkfile/exit/start/ncst/langmode/type/`bench` 미존재) 와 안 겹침.
- PIT 측정값: [timer.c:14-19](../harib27f/haribote/timer.c) 에서 divisor `0x2e9c = 11932`.
  `1193182 / 11932 ≈ 100Hz` → 한 tick 10ms. boxfill8/putfont8 같은 ㎲ 스케일에 부족 →
  per-function 카운터는 RDTSC 로 변경 (§2 결정 갱신).
- RDTSC 안전성: 단일 CPU / 단일 task 환경 (mtask 가 진행 중에도 task A 의 main loop 안에
  서만 측정). QEMU TCG 도 RDTSC 정상 에뮬 (cycle counter 가 monotonic). 직접 작성한
  `static inline uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }` 사용 예정.
- 메모리 예산:
  - `hankaku_fast[256][16][8]` = 32 KB. memman 부팅 직후 alloc — `memman_total()` 평소 ~28MB.
  - `BENCH_COUNTER` 배열 ~10개 × 32 byte ≈ 320 byte.
  - SHEET 의 `dirty_rect[4][4]` = 32 byte/sheet × 256 = 8 KB. 미미.
- HE2 ABI 호환: 기존 1~43 모두 그대로. `api_point` (11) 는 deprecated 표기만. work6
  종료 시 28개 앱 회귀 smoke 필수.
- 영향 범위 (예상 수정 파일):
  - 커널: bootpack.c, console.c, sheet.c, graphic.c, timer.c, window.c, bench.c+h(신규), bootpack.h
  - HE2 라이브러리: bxos.h, syscall.c
  - 앱: explorer/explorer.c, tetris/tetris.c, settings/settings.c (+ lines/evtest 점검)
  - 빌드/문서: CMakeLists.txt (bench.c 추가), HE2-FORMAT.md, BXOS-COMMANDS.md, README.utf8.md, SETUP-MAC.md, work6-bench.md(신규)

**Phase 0 잠금 결정 요약 (변경 시 work6.md/work6-handoff.md 동시 갱신)**

| 잠금 항목 | 값 |
|---|---|
| 측정 단위 (per-function) | RDTSC cycles (`uint64_t`) |
| 측정 단위 (per-scenario S1~S5) | PIT tick (10ms) |
| 측정 토글 | `bench on` / `bench off` / `bench reset` / `bench dump` |
| 측정 baseline 기록처 | `_doc/work6-bench.md` (Phase 1 끝에 첫 표) |
| Hot path counter slot | 9개 — refreshmap, refreshsub, sheet_slide, boxfill8, putfont8, putfonts8_asc, taskbar, scrollwin, hrb_api |
| 측정 시나리오 | S1~S5 (위) |
| Dirty rect cap | 4 rect / sheet, 5번째는 외접 합집합 |
| 폰트 expansion table | 32KB (`hankaku_fast[256][16][8]`), 부팅 시 1회 |
| 신규 syscall edx | 44=blit_rect, 45=text_run, 46=invalidate_rect, 47=dirty_flush |
| Deprecated syscall | 11 (`api_point`) — 동작 유지 |
| 성공 기준 | S1~S5 평균 ≥ 2x baseline, 회귀 0건 |
| 일정 | 9~11 작업일 |

### Phase 1 — 측정 인프라 (1.5일) — ☑ 구현 완료, QEMU baseline 측정 대기 (2026-05-01)
**목표**: 모든 다음 phase 의 효과를 숫자로 측정할 수 있게 한다.

- ☑ 신규 [harib27f/haribote/bench.c](../harib27f/haribote/bench.c) + [bench.h](../harib27f/haribote/bench.h):
  - `struct BENCH_COUNTER { const char *name; bx_u32 calls; bx_u64 total_cycles; bx_u64 max_cycles; }`.
  - `bench_enter(idx)` / `bench_leave(idx)` — RDTSC + nest stack (max 16).
  - `bench_dump(void)` → `dbg_putstr0` 로 표 출력 (calls/total/max/avg 십진수).
  - 64-bit 나눗셈은 `bench_div_u64_u32` (long division) 으로 풀어 libgcc 의존 회피.
- ☑ 9개 핵심 hot path 에 enter/leave 삽입:
  - `sheet_refreshmap`, `sheet_refreshsub`, `sheet_slide`
  - `boxfill8`, `putfont8`, `putfonts8_asc`
  - `taskbar_full_redraw`, `scrollwin_redraw`
  - `hrb_api` (wrapper + `hrb_api_inner` 분리)
- ☑ 콘솔 신규 명령 `bench` ([cmd_bench](../harib27f/haribote/console.c)):
  - `bench` — 현재 상태 + PIT tick.
  - `bench on` / `bench off` — 측정 토글.
  - `bench reset` — 카운터 0 초기화.
  - `bench dump` — debug 창에 표 출력.
  - `bench mark <label>` — debug 창에 PIT tick + 라벨 (시나리오 시작/끝 측정용).
- ☑ `bench_init()` 를 HariMain boot 에서 호출 (init_palette 직전).
- ☑ `_doc/work6-bench.md` 신규 — 측정 절차 + S1~S5 표 골격 + Phase 별 결과 슬롯.
- ☐ S1~S5 baseline 실측 (QEMU 안에서 — 사용자가 직접) + 표 채움.
- ☐ 측정 코드 자체 오버헤드 < 5% 확인 (bench off 와 on 의 idle CPU 차이).

**Phase 1 구현 노트 (2026-05-01)**
- RDTSC 사용으로 64-bit 누적값을 다뤄야 했고, freestanding 환경이라 libgcc 의 `___udivdi3`
  를 링크하지 않으므로 `bench_div_u64_u32` 을 직접 구현 (long division, 32-bit 연산만).
- `bench_enter` / `bench_leave` 의 hot path 첫 줄은 `if (!g_bench_enabled) return;`.
  `g_bench_enabled` 가 0 일 때 추가 비용은 함수 호출 1번 + load + cmp + jne. RDTSC 자체
  실행 안 됨. 작동이 검증되면 `static inline` 화 검토 가능 (Phase 2 이후).
- `hrb_api` 는 어셈에서 호출되는 entry 라 wrapper 분리. 기존 함수는 `static hrb_api_inner`
  로 변경하고 `hrb_api` 가 enter/leave + inner 호출.
- `cmd_bench` 의 `mark` 부속 명령은 PIT tick 을 debug 창에 라벨과 함께 기록 — 시나리오
  시작/끝 표시용. RDTSC 보다 PIT tick 이 시나리오 elapsed 측정에 안정적이라 둘 다 지원.
- `bench dump` 가 호출되는 동안 putfonts8/scrollwin/etc 가 다시 fire 해서 카운터를 증가
  시킨다. 시나리오 측정과 dump 순서 분리 권장 (work6-bench.md §함정).

**확인할 사항**
- ☑ Release / Debug 빌드 모두 통과, `fsck_msdos -n` clean.
- ☑ `i686-elf-readelf -S build/cmake-debug/bootpack.elf | grep debug_info` 정상 (bench.c 에도 DWARF 들어감).
- ☐ QEMU 안에서 `bench on` 후 `dir` 한 번 → `bench dump` 로 `boxfill8`/`putfont8` call
  count 가 합리적인 수치 (수백~수천).
- ☐ QEMU 안에서 `bench off` 시 hot path 추가 비용 없음 (early return).
- ☐ S1~S5 baseline 채움.

### Phase 2 — Quick wins: boxfill8 / refreshsub memcpy / ascii fast path (1일) — ☑ 구현 완료, QEMU 측정 대기 (2026-05-01)
**목표**: 단순한 변경 3개로 첫 가시적 향상.

- ☑ [`tools/modern/modern_libc.c`](../tools/modern/modern_libc.c) 의 `memset`/`memcpy`
  를 `rep stosb` / `rep movsb` inline asm 으로 교체. 호스트 검증: `objdump -d` 에서
  `f3 aa` (rep stos), `f3 a4` (rep movs) 확인.
- ☑ `boxfill8` 을 row 당 `memset` 으로. (10줄, BENCH 유지)
- ☑ `sheet_refreshsub` 의 4바이트 path / 1바이트 path **통합**. row 마다 map[] 의 sid
  run 을 byte scan 으로 찾아 memcpy. unaligned vx0 자연 처리. ([sheet.c:128](../harib27f/haribote/sheet.c))
  - `(sht->vx0 & 3) == 0` 분기 사라짐 — modern memcpy 의 rep movsb 는 src/dst 정렬에
    덜 민감하고, 큰 run 에서 byte-store 대비 5~10x.
- ☑ `putfonts8_asc_ascii(buf, xsize, x, y, c, s)` 신규 entry point ([graphic.c:187](../harib27f/haribote/graphic.c)).
  - `task_now()` / langmode 분기 / langbyte1·langbyte2 추적 없음. ascii-only 호출처에서
    직접 사용 가능.
- ☑ 기존 `putfonts8_asc` 가 langmode==0 일 때 `putfonts8_asc_ascii` 로 즉시 위임.
- ☐ `taskbar_putascii` / `menu_putascii` 통합은 보류 — 현재도 동등하게 빠르고
  bench instrumentation (BENCH_PUTFONT8) 매칭이 더 단순. work7 polish.
- ☐ Phase 1 시나리오 재측정 (QEMU 안에서 사용자 직접) + [_doc/work6-bench.md](work6-bench.md) Phase 2 표 채움.

**Phase 2 구현 노트 (2026-05-01)**
- `memset` / `memcpy` 변경의 효과는 **모든 hot path 에 누적**. boxfill8, sheet_refreshsub,
  앞으로 도입할 dirty rect 기반 redraw 가 모두 수혜.
- `sheet_refreshsub` 의 4-byte path 제거: 코드 80→30줄로 단순화. 4-byte path 의 잘
  정렬된 sid4 비교는 주로 모든 row 가 한 sheet 에 속할 때 빠름. Run-length scan + memcpy
  가 같은 case 를 1 회 검사 + 1 회 memcpy 로 처리해 동등 또는 더 빠름. partial occlusion
  case (메뉴가 윈도우 위에 떠 있을 때 등) 도 한 번의 byte scan 만 추가.
- `putfonts8_asc` 의 langmode 0 fast path 는 함수 분리만 했고 동작 동일. 그러나
  내부 hot loop 안에서 langmode 분기를 전부 dead-code 로 만들어 cache miss 줄임.
- `boxfill8` 에 width 음수(`x1 < x0`) 보호 추가 — 이전 byte loop 은 0-iteration 으로
  자연 보호됐지만 memset 은 size_t 가 음수가 안 되므로 명시 처리.
- `sheet_refreshmap` 의 4-byte path 일반화는 Phase 3 으로 넘김 (compositor 일반화 묶음).
  refreshmap 은 hot 한 정도가 refreshsub 의 1/3 수준이라 우선순위 낮음.

**확인할 사항**
- ☑ Release / Debug 빌드 통과, `fsck_msdos` clean.
- ☑ `objdump -d` 에서 `_memset` 에 `f3 aa` (rep stos), `_memcpy` 에 `f3 a4` (rep movs) 확인.
- ☐ QEMU smoke (사용자 직접):
  - 부팅 직후 화면 그려짐 정상 (taskbar / Start 버튼).
  - 콘솔에서 `dir`, `mem`, `cls`, `langmode 3 + type hangul.euc`, `langmode 4 + type hangul.utf` 회귀 없음.
  - explorer / tetris / lines / evtest 회귀 없음.
- ☐ S1~S5 baseline 대비 1.5배 이상 빠름.

### Phase 3 — Compositor 일반화 + putfont 마스크 lookup (1.5일) — ☑ 구현 완료, QEMU 측정 대기 (2026-05-01)
**목표**: 컴포지터의 모든 path 에 memset/memcpy fast path + 폰트 그리기 branchless.

- ☑ `sheet_refreshmap` 의 4-byte / 1-byte 분기 통합 ([sheet.c:75](../harib27f/haribote/sheet.c)):
  - opaque sheet (col_inv == -1) → row 한 줄 통째로 `memset(map, sid, len)`.
    `(sht->vx0 & 3) == 0` 조건 제거. 모든 정렬 처리.
  - transparent sheet (col_inv != -1) → buf 의 non-col_inv run 을 byte scan 후 `memset(map, sid, run_len)`.
- ☑ `putfont8` 의 비트 lookup 도입 ([graphic.c](../harib27f/haribote/graphic.c)):
  - `static unsigned int g_putfont_mask_lo[256], g_putfont_mask_hi[256]` (256 × 2 ints = 2 KB).
  - 부팅 시 `putfont_mask_init()` 가 한 번 채움. (work6.md 의 32KB 계획 — 실제로는 더 효율적인 2KB 마스크 lookup 으로 축소.)
  - hot loop: row 마다 byte d → `mask_lo = g_putfont_mask_lo[d]`, `mask_hi = g_putfont_mask_hi[d]`.
    32-bit `(p4[0] & ~m_lo) | (c4 & m_lo)` 두 번 — branchless. row 당 분기 0.
- ☑ HariMain boot sequence: `bench_init` → `putfont_mask_init` → `init_palette` → ... 순.
- ☐ Phase 1 시나리오 재측정 (사용자 직접) + work6-bench.md Phase 3 표 채움.

**Phase 3 구현 노트 (2026-05-01)**
- 원래 work6.md §2 의 계획은 `hankaku_fast[256][16][8]` 32 KB lookup. 그러나 글자별로
  16 row × 8 byte 를 미리 펼쳐 두면 row 마다 4-pixel 마스크 두 개를 그대로 store
  하면 되지만, 매 글자 마다 16 × 8 = 128 byte 의 cache line 을 끌어다 쓴다 (2 cache
  lines per char). 대신 8-bit row pattern → 8-byte mask 의 256-entry lookup 만 두면
  글자당 16 row × 1 byte 의 hankaku 를 그대로 읽어 mask 두 번 lookup. memory 25 ×
  + cache 친화. 동작은 동일.
- `_putfont8` 의 disassembly 확인: 행 본문에 conditional branch 없음 (`jne` 는 outer
  `i < 16` 루프 한 번만). `_sheet_refreshmap` 도 `call _memset` 이 row 당 1회로 빠짐.
- `sheet_refreshmap` 의 boundary check 추가: `bx0 >= bx1 || by0 >= by1` 인 경우 즉시
  continue. 이전 byte loop 은 자연 0-iteration 이었지만 memset 은 size_t 음수 회피 위해
  명시 처리.
- 비-정렬 32-bit access: x86 hardware 가 처리. taskbar/menu 라벨은 8-px 정렬, 앱
  putstrwin 도 일반적으로 정렬되어 fast path. 정렬 안 되어도 동작은 정상.

**확인할 사항**
- ☑ Release / Debug 빌드 통과, `fsck_msdos` clean.
- ☑ `objdump -d` 에서 `_putfont8` 의 row 본문에 conditional branch 0건 (`jne` outer 루프 한 개만).
- ☑ `_sheet_refreshmap` 가 `call _memset` 사용.
- ☐ QEMU smoke (사용자 직접):
  - 부팅 직후 화면 / Start 버튼 / 시계 / 메뉴 라벨 글자 깨짐 없음.
  - `langmode 3 + type hangul.euc`, `langmode 4 + type hangul.utf` 회귀 없음 (한글 폰트는 `putfonts8_asc` 를 쓰지만 안에서 `putfont8` 호출 → 마스크 lookup 동일 적용).
- ☐ S1~S5 누적 baseline 대비 2.5배 이상.

### Phase 4 — Dirty rect 인프라 (커널 측) (2일)
**목표**: sheet 단위 부분 갱신 시스템 도입. 기존 `sheet_refresh` 호출과 호환.

- ☐ [bootpack.h](../harib27f/haribote/bootpack.h) `struct SHEET` 에:
  - `int dirty_count;` (0~4)
  - `int dirty_rect[4][4];`  // x0,y0,x1,y1
- ☐ [sheet.c](../harib27f/haribote/sheet.c) 신규 함수:
  - `void sheet_dirty_add(struct SHEET *sht, int bx0, int by0, int bx1, int by1)` — 누적, 4개 초과 시 union 합침.
  - `void sheet_dirty_flush(struct SHEET *sht)` — 누적 영역들을 한 번에 refresh, count=0.
  - `void sheet_dirty_flush_all(struct SHTCTL *ctl)` — 모든 sheet 의 dirty 비움. HariMain idle 진입 직전 호출.
- ☐ `sheet_refresh` 의 동작:
  - 기본은 즉시 refresh 유지 (호환).
  - 내부 helper `sheet_refresh_now()` 와 `sheet_refresh_deferred()` 로 분기. App syscall 12 (api_refreshwin) 는 즉시.
- ☐ HariMain 의 main loop 의 idle 분기에 `sheet_dirty_flush_all(shtctl)` 호출.
- ☐ Phase 1 시나리오 재측정. 이 단계에서는 호환 모드라 큰 변화 없을 수 있음 (기반만 까는 단계).

**확인할 사항**
- ☐ build 통과, 회귀 없음.
- ☐ dirty_count 4 초과 시 union 동작 확인 (단위 테스트성 시나리오).

### Phase 5 — Taskbar / 마우스 / scrollwin 부분 redraw (2일)
**목표**: Phase 4 인프라 위에서 가장 큰 redraw 들을 부분 갱신으로.

- ☐ [bootpack.c](../harib27f/haribote/bootpack.c) `taskbar_full_redraw` →
  - `taskbar_redraw_start(hover, pressed)` — Start 버튼 영역만.
  - `taskbar_redraw_clock()` — tray 영역만.
  - `taskbar_redraw_winlist(hover, pressed)` — winlist 가운데 영역만.
  - 통합 `taskbar_full_redraw` 는 위 3개 호출 후 dirty flush.
- ☐ Hover 변화 시 변한 인덱스 버튼만 dirty 추가.
- ☐ Clock tick 시 tray 만 dirty 추가.
- ☐ [console.c](../harib27f/haribote/console.c) scrollwin:
  - `scrollwin_redraw_lines(start, n)` — n 줄만 다시 그림.
  - `scrollwin_redraw_scrollbar()` — scrollbar 만.
  - `scrollwin_append_line(line)` — 마지막에 한 줄 그리고 dirty 만 그 영역.
  - 콘솔 출력 (`cons_putchar` / scroll) 이 append 사용.
- ☐ 마우스 sheet 의 sheet_slide:
  - 같은 정수 좌표 skip (이미 마우스가 동일 픽셀이면 no-op).
- ☐ Phase 1 시나리오 재측정.

**확인할 사항**
- ☐ S1, S2 가 baseline 대비 3배 이상.
- ☐ S5 (마우스) 가 baseline 대비 3배 이상.
- ☐ scrollwin 스크롤 정상, 콘솔 출력 정상, taskbar hover 부드러움.

### Phase 6 — App 측 syscall 추가 / 정리 (1.5일)
**목표**: 부분 갱신 + batch 를 앱이 활용할 수 있게 ABI 확장. 기존 호환은 그대로.

- ☐ 커널 syscall 추가:
  - **edx 44 — `api_blit_rect`**:
    - `(win, src_buf, sx, sy, sw, sh, dst_x, dst_y) → 0/-1`
    - 사용자 buffer 에서 window 의 client area 일부분으로 복사. col_inv 무시 (raw blit).
    - tetris 의 board buffer, 이미지 뷰어 등에 유용.
  - **edx 45 — `api_text_run`**:
    - `(win, x, y, color, text, len, defer) → drawn_chars`
    - ASCII 전용 fast path. 한 syscall 로 길이 텍스트 출력 + dirty 1개.
    - explorer 의 row label, settings 의 옵션 라벨 등.
  - **edx 46 — `api_invalidate_rect`**:
    - `(win, x0, y0, x1, y1)` — 그리지 않고 dirty 만 누적.
    - 앱이 여러 차례 boxfill 후 마지막에 한 번 flush.
  - **edx 47 — `api_dirty_flush`**:
    - `(win)` — 해당 window 의 누적 dirty 즉시 refresh.
    - 명시적 batch 종료.
- ☐ HE2 user wrapper:
  - [he2/libbxos/include/bxos.h](../he2/libbxos/include/bxos.h) prototype.
  - [he2/libbxos/src/syscall.c](../he2/libbxos/src/syscall.c) inline asm.
  - [he2/docs/HE2-FORMAT.md](../he2/docs/HE2-FORMAT.md) syscall 표 갱신 (1~47).
- ☐ `api_point` (edx 11) 에 deprecation 주석 추가. 동작 유지.

**확인할 사항**
- ☐ libbxos.a 다시 빌드, 모든 기존 앱 회귀 없음.
- ☐ 신규 syscall 4종이 호스트 검증으로 동작 (간단한 테스트 앱 1개 추가 — `bench_blit.he2`).

### Phase 7 — App side polish (1.5일)
**목표**: 핵심 앱들이 새 ABI / batch 를 활용하도록 정리.

- ☐ **explorer**:
  - `redraw_all` → row 단위 분리. tree row, list row, status, toolbar 각자.
  - row 한 줄 변할 때 그 row 만 redraw + invalidate_rect.
  - `cli_refresh` 호출 횟수 70% 감소 목표.
- ☐ **tetris**:
  - 보드 grid → off-screen buffer 유지. 한 라인 클리어 시 `api_blit_rect` 한 번.
  - score / level 텍스트는 변할 때만 갱신.
- ☐ **settings**:
  - 카테고리 클릭 시 우측 페이지만 redraw. 좌측 트리는 그대로.
  - widget 변화 시 그 widget 영역만.
- ☐ **콘솔 (커널 측 console_task)**:
  - Phase 5 의 scrollwin_append_line 활용.
- ☐ **lines / evtest / sosu** (polish):
  - bit0=1 deferred flag 사용 점검. 누락된 곳 정리.
- ☐ Phase 1 시나리오 재측정. **성공 기준 평가** (§2: 2배 이상).

**확인할 사항**
- ☐ S1~S5 모두 baseline 대비 2배 이상.
- ☐ explorer 100개 entry 디렉터리 listing 부드러움.
- ☐ tetris 게임 플레이 회귀 없음.

### Phase 8 — 회귀 검증 / 측정 보고 / 문서 (1일)
- ☐ 전체 QEMU smoke (work5 Phase 8 의 모든 항목 + work6 측정 시나리오 5개).
- ☐ [BXOS-COMMANDS.md](../BXOS-COMMANDS.md) 에 `bench` 명령 / 새 syscall 효과 단락.
- ☐ [README.utf8.md](../README.utf8.md) 에 "성능 / 측정" 한 단락.
- ☐ [SETUP-MAC.md](../SETUP-MAC.md) 에 `bench` 사용법.
- ☐ [he2/README.md](../he2/README.md) edx 1~47 갱신, deprecation 메모.
- ☐ [he2/docs/HE2-FORMAT.md](../he2/docs/HE2-FORMAT.md) 신규 syscall 표.
- ☐ 신규 [_doc/work6-bench.md](work6-bench.md): baseline / phase 별 측정값 + S1~S5 정의.
- ☐ work6.md / work6-handoff.md 체크박스 갱신.

## 4. 마일스톤 / 검증 시나리오

| 끝난 시점 | 측정 시나리오 검증 |
|---|---|
| Phase 1 | bench 명령 동작. baseline 기록 (`bench dump` 표). |
| Phase 2 | S1~S5 모두 ≥ 1.5x baseline. |
| Phase 3 | S1~S5 누적 ≥ 2.5x baseline. |
| Phase 4 | 회귀 없음 (인프라 단계). |
| Phase 5 | S1, S2, S5 ≥ 3x baseline. |
| Phase 6 | 새 syscall 4종 동작 확인 (test app). |
| Phase 7 | **성공 기준 평가**. S1~S5 평균 ≥ 2x baseline. |
| Phase 8 | 모든 회귀 smoke + 문서 완료. |

### 측정 시나리오 정의

- **S1**: 부팅 직후 → Ctrl+Esc 토글 10회 → Start 종료까지의 elapsed PIT count.
  - 목적: taskbar / menu redraw 비용.
- **S2**: 콘솔에서 `dir` 명령 10연속 (각 명령 사이 100ms wait) elapsed.
  - 목적: scrollwin / 텍스트 출력 비용.
- **S3**: explorer 띄우고 `/` 의 50개 entry tree+list 첫 그리기 elapsed.
  - 목적: 앱 측 redraw + putfonts8_asc + boxfill8 합산.
- **S4**: tetris 한 판에서 한 라인 클리어 시점에 보드 redraw elapsed.
  - 목적: 게임 응답성 / blit_rect (Phase 6+) 효과.
- **S5**: 화면 좌→우 → 마우스 1000회 미세 이동의 sheet_slide 누적 ticks.
  - 목적: 마우스 hover 비용.

## 5. 위험 요소 / 함정

- **PIT 측정 정밀도**: 10ms tick 이라 짧은 hot path 는 분해능 부족. call count + 누적 ticks 로 보완. RDTSC 사용은 회피 (cross-task / 비교환경 변동).
- **memset / memcpy 의존**: i686-elf-gcc 의 builtin 이 inline 으로 들어가는지 (`-fno-builtin` 영향) 확인. 안 들어가면 직접 `rep stosb`/`rep movsb` 어셈 helper.
- **Dirty rect union 정확성**: 두 rect 의 union 은 단순히 외접 사각형 → 처음 그리지 않은 픽셀까지 refresh 가능. 4개로 cap → union → 효율 손실 약간.
- **scrollwin append 의 스크롤 처리**: append 가 viewport 끝을 넘으면 전체 line shift 가 필요. 그 경우는 full redraw 로 fallback.
- **마우스 sheet_slide skip 의 race**: HariMain 단일 task 라 race 없음. 단지 같은 좌표 비교만.
- **새 syscall 의 보안**: `api_blit_rect` 의 src_buf 가 ds_base 안인지 boundary check (sht 도 task 소유 확인).
- **HE2 ABI 호환**: 기존 앱 28개가 edx 1~43 그대로 동작하는지 회귀.
- **bench 코드 자체 오버헤드**: bench off 시 inlined return 만 남도록 매크로 또는 `__attribute__((always_inline)) static inline`.
- **putfont expansion table 의 메모리**: 32KB. memman 에서 부팅 직후 한 번 alloc. 회귀 시 freeable.
- **explorer 의 splitter / row 단위 redraw**: 부분 redraw 도입 후 splitter drag 시 잔상 가능성. 충분히 테스트.
- **syscall 번호 충돌**: edx 44~47 잠금. Phase 6 까지 변경 금지.

## 6. 범위 외 (이번 작업에서 안 하는 것)

- 사용자 모드 라이브러리 OS-level 변경 (signal, IPC).
- multi-process / fork 모델.
- mouse cursor 모양 다양화 (이미 4종 충분).
- VGA 16색 → 256색 / true color 전환 (해상도 변경 syscall 없음).
- network / sound / printer.
- compositor 의 GPU acceleration (QEMU TCG 한계).
- HE2 → ELF 직접 로드.
- HE2 앱 hot-reload.
- syscall ABI 의 안정화 (work6 기간엔 변경 가능, work7 부터 freeze 검토).

## 7. 예상 일정

총 **9~11 작업일** (1인 풀타임). 부담 큰 phase 는 Phase 4 (dirty rect 인프라) 와
Phase 7 (앱 측 polish — 분량 많음). Phase 1 측정 인프라가 가장 먼저 완료되어야
이후 phase 들의 효과를 객관적으로 평가할 수 있다.

| Phase | 분량 | 핵심 산출물 |
|---|---|---|
| 0. 요구사항 / 인터페이스 | 0.5d | 측정 시나리오, syscall 번호 잠금 |
| 1. 측정 인프라 | 1.5d | bench.c, bench.h, `bench` 콘솔 명령, baseline 기록 |
| 2. Quick wins | 1d | boxfill8 memset, refreshsub memcpy, ascii fast path |
| 3. Compositor 일반화 + 폰트 lookup | 1.5d | refreshmap 4-byte 일반화, hankaku_fast[] |
| 4. Dirty rect 인프라 | 2d | sheet_dirty_*, idle flush, 호환 mode |
| 5. Taskbar / scrollwin / 마우스 부분 redraw | 2d | 3 곳 모두 부분 갱신 |
| 6. App 측 syscall 추가 / 정리 | 1.5d | edx 44~47, libbxos 갱신 |
| 7. App side polish | 1.5d | explorer / tetris / settings / console |
| 8. 회귀 / 문서 | 1d | BXOS-COMMANDS / README / SETUP / he2 / work6-bench |

## 8. 신규 / 변경 syscall 표 (work6)

기존 1~43 은 work5 그대로 유지. work6 신규:

| edx | 이름 | 설명 |
|---|---|---|
| 44 | `api_blit_rect` | 사용자 buffer rect → window client area rect (raw, col_inv 무시). |
| 45 | `api_text_run` | ASCII fast path 로 텍스트 + 색 + dirty 1개. (defer flag 지원) |
| 46 | `api_invalidate_rect` | window client rect 를 dirty 만 누적 (그리지 않음). |
| 47 | `api_dirty_flush` | window 의 누적 dirty 를 즉시 refresh. |
| 11 | `api_point` (existing) | **DEPRECATED** — 픽셀당 syscall + refresh 라 매우 느림. 신규 코드는 `api_blit_rect` 또는 `api_invalidate_rect` + `api_dirty_flush`. |

신규 syscall 의 호환성 메모:
- 기존 `api_refreshwin` (12) / `api_putstrwin` (6) / `api_boxfilwin` (7) 의 deferred flag (bit0=1) 는 그대로 동작. work6 의 신규 syscall 은 deferred + dirty 통합 모델로 더 명시적.
- `api_text_run` 는 langmode 1~4 에서는 첫 글자에서 멈춰야 하는지 / 그래도 ASCII 부분만 그리고 나머지 무시할지 — 현재 spec 은 "non-ASCII 만나면 거기서 stop, drawn_chars 반환".

이 표는 Phase 6 시작 시 잠금. 변경 시 work6.md / work6-handoff.md / he2/docs/HE2-FORMAT.md 동시 갱신.
