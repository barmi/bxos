# work6 — 측정 baseline / phase 별 결과

work6 의 GUI 효율화 phase 들이 누적되는 과정을 같은 시나리오 5개로 수치 비교.
첫 표는 Phase 1 종료 직후 측정한 baseline (work5 종료 시점 + bench 인프라만 추가된 상태).

## 측정 도구

* **Per-function counter**: RDTSC cycle (1 cycle = 1ns @ 1GHz emulation, QEMU TCG 는 비슷한 수준).
  콘솔 `bench on/off/reset/dump` 으로 토글.
  * `bench dump` 는 9 개 hot path 의 `calls / total / max / avg` 를 debug 창에 출력.
* **Per-scenario elapsed**: PIT 10ms tick (`timerctl.count`).
  콘솔 `bench mark <label>` 으로 시작/종료 마크. 차이 = 시나리오 elapsed (단위 ticks × 10ms).

## 측정 시나리오 (S1~S5)

| ID | 시나리오 | 실행 절차 | 측정 |
|---|---|---|---|
| S1 | Start menu 토글 | 콘솔 `bench mark s1-start` → Ctrl+Esc 10회 → `bench mark s1-end` | end - start (PIT ticks) |
| S2 | 콘솔 dir × 10 | `bench reset` → `bench on` → `dir` ↩ 10번 → `bench dump` | scrollwin/putfont 카운터 |
| S3 | explorer / 첫 그리기 | `bench reset; bench on` → Start → Programs → Explorer → 디렉터리 펼침 후 `bench dump` | refreshmap/refreshsub/putfonts 카운터 |
| S4 | tetris 1 라인 클리어 | `bench reset; bench on` → Start → Programs → Games → Tetris → 1 라인 클리어 후 `bench dump` | hrb_api/refreshsub 카운터 |
| S5 | 마우스 1000 step | `bench reset; bench on` → 마우스 한 번 좌→우 (대략 1000 픽셀) → `bench dump` | sheet_slide 카운터 |

각 시나리오마다 **bench reset → 시나리오 → bench dump → 결과 캡처** 순서.
toggle/reset 사이의 잔여 카운터 영향은 reset 으로 제거.

## 측정 절차 (reproducible)

```text
1. ./run-qemu.sh  (Release 빌드 권장 — Debug 는 -O0 라 cycle 카운트가 의미 다름)
2. 부팅 후 Start → Programs → Console.
3. 콘솔에서 bench on
4. (시나리오 실행)
5. bench dump
6. Start → Programs → Debug 으로 debug 창을 열고 출력된 표 확인.
   (debug 창은 `dbg_putstr0` 의 누적이라 닫혀 있어도 데이터는 보존됨.)
```

## 표 — Phase 1 baseline (2026-05-02)

> Phase 1 의 측정 인프라 자체 외 다른 변경이 없는 commit (`abe3566`) 시점.
> RDTSC cycles 단위. QEMU TCG / Apple Silicon M-series host. CPU emulation rate 는
> QEMU 측정값에 의존. 절대값은 호스트마다 다를 수 있어, 다음 phase 와 **상대 비교**
> 만이 의미 있음.

> 측정 절차: 각 시나리오 직전에 `bench reset; bench on` → 시나리오 → `bench mark
> sN-end` → `bench dump`. S1 만 `s1-start` 마크도 함께 (elapsed 계산용). S2~S5 는
> reset 직후가 사실상 시작점이라 별도 start 마크 생략.

### 모든 카운터 한눈에 보기 (전체 hot path × 5 시나리오)

| counter | S1 menu×10 | S2 dir×10 | S3 explorer | S4 tetris-line | S5 mouse |
|---|---:|---:|---:|---:|---:|
| `refreshmap` calls | 60 | 0 | 688 | 2,648 | 872 |
| `refreshmap` total cycles | 31,733,000 | 0 | 23,354,000 | 86,994,000 | 20,714,000 |
| `refreshmap` avg | 528,883 | — | 33,944 | 32,852 | 23,754 |
| `refreshsub` calls | 534 | 20,019 | 1,184 | 4,366 | 1,321 |
| `refreshsub` total cycles | 514,511,000 | 16,605,969,000 | 479,370,000 | 2,393,861,000 | 436,674,000 |
| `refreshsub` avg | 963,503 | 829,510 | 404,873 | 548,296 | 330,563 |
| `sheet_slide` calls | 30 | 0 | 342 | 1,317 | 434 |
| `sheet_slide` total cycles | 2,441,000 | 0 | 53,497,000 | 200,262,000 | 66,082,000 |
| `boxfill8` calls | 1,288 | 311 | 2,284 | 27,245 | 165 |
| `boxfill8` total cycles | 15,615,000 | 2,631,000 | 22,600,000 | 61,979,000 | 1,865,000 |
| `boxfill8` avg | 12,123 | 8,459 | 9,894 | 2,274 | 11,303 |
| `putfont8` calls | 77,044 | 4,630,501 | 256,940 | 285,721 | 277,259 |
| `putfont8` total cycles | 28,680,000 | 1,379,479,000 | 92,585,000 | 102,180,000 | 96,413,000 |
| `putfont8` avg | 372 | 297 | 360 | 357 | 347 |
| `putfonts8_asc` calls | 0 | 0 | 609 | 92 | 0 |
| `putfonts8_asc` total | 0 | 0 | 3,185,000 | 893,000 | 0 |
| `taskbar` calls | 30 | 2 | 11 | 28 | 1 |
| `taskbar` total cycles | 46,022,000 | 1,997,000 | 14,563,000 | 40,179,000 | 1,310,000 |
| `taskbar` avg | 1,534,066 | 998,500 | 1,323,909 | 1,434,964 | 1,310,000 |
| `scrollwin` calls | 669 | 20,116 | 697 | 751 | 679 |
| `scrollwin` total cycles | 1,559,861,000 | 30,044,286,000 | 1,587,196,000 | 1,815,562,000 | 1,654,211,000 |
| `scrollwin` avg | 2,331,630 | 1,493,551 | 2,277,182 | 2,417,525 | 2,436,215 |
| `hrb_api` calls | 0 | 0 | 2,305 | 28,520 | 0 |
| `hrb_api` total cycles | 0 | 0 | 2,599,075,000 | 41,239,068,000 | 0 |
| `hrb_api` avg | — | — | 1,127,581 | 1,445,970 | — |

### S1 — Start 메뉴 토글 10회

| 항목 | baseline |
|---|---|
| PIT ticks (s1-start=12858, s1-end=15889) | **3,031 ticks (~30.3 s wallclock)** |
| dump pit_tick | 16,654 |
| `taskbar` calls / total / max / avg | 30 / 46,022,000 / 2,911,000 / 1,534,066 |
| `refreshmap` calls / total / avg | 60 / 31,733,000 / 528,883 |
| `refreshsub` calls / total / avg | 534 / 514,511,000 / 963,503 |
| `sheet_slide` calls / total | 30 / 2,441,000 |
| `putfont8` calls / total | 77,044 / 28,680,000 |
| `putfonts8_asc` calls | 0 (Start 라벨/시계는 `taskbar_putascii` → `putfont8` 직접) |
| `scrollwin` calls / total | 669 / 1,559,861,000 |
| `boxfill8` calls / total | 1,288 / 15,615,000 |

> S1 의 30 초 elapsed 는 사용자가 Ctrl+Esc 토글 10회 사이에 인터랙션 간격을
> 둔 시간 포함. 카운터 합으로 보면 실제 menu redraw 작업은 ~2.2 G cycles ≈ 2.2 s.
> 의외로 `scrollwin` total 이 가장 큼 — 부팅 시 콘솔 task 가 자체 cursor 깜박임으로
> scrollwin_redraw 를 백그라운드에서 호출 중 (Phase 5 부분 redraw 의 큰 타깃 1).

### S2 — 콘솔 `dir` × 10

| 항목 | baseline |
|---|---|
| dump pit_tick | 44,276 (s2-end mark = 43,377) |
| `scrollwin` calls / total / max / avg | 20,116 / 30,044,286,000 / 9,565,000 / 1,493,551 |
| `putfont8` calls / total / avg | 4,630,501 / 1,379,479,000 / 297 |
| `boxfill8` calls / total / avg | 311 / 2,631,000 / 8,459 |
| `refreshsub` calls / total / avg | 20,019 / 16,605,969,000 / 829,510 |
| `taskbar` calls / total | 2 / 1,997,000 |
| `hrb_api` calls | 0 |
| `putfonts8_asc` calls | 0 |

> **dir × 10 의 hot 함수는 `scrollwin`(30 G cycles)** + `refreshsub`(16.6 G cycles)
> + `putfont8` 4.6 M 호출(1.38 G cycles). scrollwin 한 번이 평균 1.5 M cycles 인
> 이유 — 매 `cons_putchar` 마다 viewport 전체를 다시 그리는 기존 알고리즘.
> Phase 5 의 `scrollwin_append_line` 이 가장 큰 효과를 낼 시나리오.

### S3 — explorer `/` 첫 그리기

| 항목 | baseline |
|---|---|
| dump pit_tick | 60,109 (s3-end = 59,249) |
| `refreshmap` calls / total / avg | 688 / 23,354,000 / 33,944 |
| `refreshsub` calls / total / avg | 1,184 / 479,370,000 / 404,873 |
| `putfonts8_asc` calls / total / avg | 609 / 3,185,000 / 5,229 |
| `putfont8` calls / total | 256,940 / 92,585,000 |
| `boxfill8` calls / total | 2,284 / 22,600,000 |
| `hrb_api` calls / total / avg | 2,305 / 2,599,075,000 / 1,127,581 |
| `sheet_slide` calls / total | 342 / 53,497,000 |
| `scrollwin` calls / total | 697 / 1,587,196,000 |

> `hrb_api` 가 시나리오 합의 50% (~2.6 G cycles). explorer 가 `api_boxfilwin` /
> `api_putstrwin` / `api_refreshwin` 을 매우 자주 호출. Phase 6 의 `api_text_run` /
> `api_invalidate_rect` / `api_dirty_flush` 의 직접 타깃.
> `refreshsub` 비용 (479 M) 도 큼 — Phase 4/5 dirty rect 부분 갱신 효과 검증 지점.

### S4 — tetris 1 라인 클리어

| 항목 | baseline |
|---|---|
| dump pit_tick | 81,525 (s4-end = 81,096) |
| `hrb_api` calls / total / avg | 28,520 / 41,239,068,000 / 1,445,970 |
| `boxfill8` calls / total / avg | 27,245 / 61,979,000 / 2,274 |
| `refreshsub` calls / total / avg | 4,366 / 2,393,861,000 / 548,296 |
| `refreshmap` calls / total | 2,648 / 86,994,000 |
| `sheet_slide` calls / total | 1,317 / 200,262,000 |
| `putfont8` calls / total | 285,721 / 102,180,000 |
| `putfonts8_asc` calls | 92 |
| `taskbar` calls / total | 28 / 40,179,000 |

> tetris 는 `hrb_api` 하나만 41 G cycles. 매 cell drop / line clear 마다 `api_boxfilwin`
> 수십 회 호출. `api_blit_rect` 도입 (Phase 6) + tetris 의 boxfilwin 묶음 개편 (Phase 7) 으로
> 큰 폭 개선 예상. 단일 시나리오 비용 1위.

### S5 — 마우스 1000 step

| 항목 | baseline |
|---|---|
| dump pit_tick | 94,206 (s5-end = 93,455) |
| `sheet_slide` calls / total / avg | 434 / 66,082,000 / 152,262 |
| `refreshmap` calls / total / avg | 872 / 20,714,000 / 23,754 |
| `refreshsub` calls / total / avg | 1,321 / 436,674,000 / 330,563 |
| `boxfill8` calls / total | 165 / 1,865,000 |
| `putfont8` calls / total | 277,259 / 96,413,000 |
| `scrollwin` calls / total | 679 / 1,654,211,000 |
| `hrb_api` calls | 0 |

> 마우스 이동 1 step 당 `sheet_slide` ~1 회 (434 슬라이드 = 약 217 mouse-move 이벤트).
> 슬라이드 한 번이 평균 152 K cycles. 그러나 sheet_slide 안에서 `refreshmap` x2 +
> `refreshsub` x2 가 누적되어 row 처리 비용이 더 큼 (refreshsub 437 M 가 dominant).
> 마우스 sheet 의 cursor → background blit 만 부분 갱신 가능 (Phase 5).

## 핵심 hot-spot 랭킹 (전체 5 시나리오 합)

각 시나리오의 dominant counter 비중. **개선 우선순위 가이드**.

| counter | S1 | S2 | S3 | S4 | S5 | 합 (cycles) | 개선 phase |
|---|---:|---:|---:|---:|---:|---:|---|
| `hrb_api` | — | — | 2.6 G | **41.2 G** | — | 43.8 G | Phase 6/7 |
| `scrollwin` | 1.6 G | **30.0 G** | 1.6 G | 1.8 G | 1.7 G | 36.7 G | Phase 5 |
| `refreshsub` | 0.5 G | **16.6 G** | 0.5 G | 2.4 G | 0.4 G | 20.4 G | Phase 2/4 |
| `putfont8` | 28.7 M | **1.4 G** | 92.6 M | 102 M | 96.4 M | 1.7 G | Phase 3 |
| `refreshmap` | 31.7 M | 0 | 23.4 M | 87.0 M | 20.7 M | 162.8 M | Phase 3 |
| `taskbar` | 46 M | 2 M | 14.6 M | 40.2 M | 1.3 M | 104.1 M | Phase 5 |
| `sheet_slide` | 2.4 M | 0 | 53.5 M | 200 M | 66.1 M | 322 M | Phase 5 (skip same coord) |
| `boxfill8` | 15.6 M | 2.6 M | 22.6 M | 62.0 M | 1.9 M | 104.7 M | Phase 2 (memset) |
| `putfonts8_asc` | 0 | 0 | 3.2 M | 0.9 M | 0 | 4.1 M | Phase 2 (ascii fast) |

**관찰:**

1. **`hrb_api` (43.8 G) 와 `scrollwin` (36.7 G) 가 양대 hot spot**. tetris/콘솔 은 거의 이 둘에 좌우.
2. **`scrollwin`** 은 매 호출에 viewport full redraw — Phase 5 의 `scrollwin_append_line` 이 효과 최대.
3. **`hrb_api`** 는 syscall 빈도 자체가 문제 — Phase 6 의 batch syscall (`api_text_run`, `api_blit_rect`, `api_invalidate_rect`+`api_dirty_flush`) 의 직접 타깃.
4. **`putfont8` 4.6 M 호출 (S2)** — scrollwin 안에서 매 글자마다. 절대 cycles 가 1.4 G 라 작지 않음. Phase 3 의 마스크 lookup 이 ~3-5x 줄여줄 후보.
5. **`refreshsub` (20.4 G)** — Phase 2 의 run-length memcpy + Phase 4 의 dirty rect 가 합성 효과.
6. **`boxfill8` (105 M)** — 절대값은 작지만 호출 빈도 (32 K) 가 높음. Phase 2 의 row memset 이 효과 큼.
7. `putfonts8_asc` 는 거의 호출 안 됨 (4 M cycles) — 콘솔/tetris/explorer 모두 `putfont8` 을 직접 호출. **`putfonts8_asc` 의 ascii fast path 효과는 새로 위임받는 호출처가 있을 때만** (현재 거의 없음).

> 정리: Phase 5 (scrollwin 부분 redraw) + Phase 6/7 (hrb_api batch) 가 시나리오
> 합 시간의 **80% 이상** 을 차지. Phase 2/3 의 cycles 절감은 단일 함수 단위로
> 측정될 것이고, 큰 그림은 Phase 5+ 의 호출 빈도 감소가 결정한다.

## bench off 오버헤드 검증

bench 코드 자체가 hot path 에 비용을 추가하면 baseline 이 왜곡됨. 따라서 측정 코드는
**off 일 때 추가 비용 0 에 가까워야** 한다.

검증 방법:
1. `bench off` 상태에서 S1 시나리오 (PIT ticks).
2. 같은 시나리오를 work5 종료 시점 binary 로 측정 (bench 인프라 없는 commit).
3. 두 값 차이가 `< 5%` 면 OK.

(실제 측정은 QEMU 부팅 시 진행. 결과는 아래에 채움.)

| 측정 | bench off | work5 종료 binary | 차이 |
|---|---|---|---|
| S1 PIT ticks | (실측) | (실측) | (계산) |

오버헤드의 원리: `bench_enter` / `bench_leave` 의 첫 줄이 `if (!g_bench_enabled) return;`.
g_bench_enabled 가 0 이면 함수 호출 1번 + ld + cmp + jne 정도. 백만 호출당 ~수 ms 수준.

## 나중 phase 결과 (Phase 2 ~ 8 종료 시 기록)

각 phase 종료마다 같은 표를 다시 채워 비교한다.

### 표 — Phase 2 (Quick wins) 후

(Phase 2 종료 시 채움)

### 표 — Phase 3 (compositor 일반화 + 폰트 lookup) 후

(Phase 3 종료 시 채움)

### 표 — Phase 4 (Dirty rect 인프라) 후

### 표 — Phase 5 (taskbar/scrollwin/마우스 부분 redraw) 후

### 표 — Phase 6 (앱 측 syscall 추가) 후

### 표 — Phase 7 (앱 측 polish) 후

### 표 — Phase 8 (회귀/문서) 후

## 성공 기준 (work6.md §2)

* S1~S5 평균이 baseline 대비 **2배 이상** 빨라짐 (총 PIT ticks 또는 총 cycles).
* 회귀 0건 — work5 의 모든 동작이 정상.

## 알려진 측정 함정

* **RDTSC 첫 호출** 이 cold cache 라 max_cycles 가 1 회만 비정상적으로 큼. 시나리오 시작
  전에 한두 번 warm-up (예: 마우스 한 번 움직임) 권장.
* **QEMU TCG 의 RDTSC** 는 emulated CPU cycle. host CPU 부하/스케줄러에 따라 흔들림.
  같은 호스트에서 같은 옵션으로 비교해야 함.
* **Debug 빌드 (`-O0 -g`)** 는 hot path 함수가 인라인 안 되고 stack frame 이 커서 cycle
  카운트가 Release 의 2~3배. 측정은 항상 Release 빌드 (`-DCMAKE_BUILD_TYPE` 없는 default).
* **`bench dump` 자체** 가 많은 putfonts8/dbg_putstr0 호출을 일으킴. dump 직전에 reset
  하지 말고, 시나리오 실행 → dump → 다음 reset 순서.
* **부팅 직후의 측정값** 에는 init_screen / init_palette 등이 포함됨. 안정화 후 reset 권장.
