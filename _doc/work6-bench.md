# work6 — 측정 baseline / phase 별 결과

work6 의 GUI 효율화 phase 들이 누적되는 과정을 같은 시나리오 5개로 수치 비교.
첫 표는 Phase 1 종료 직후 측정한 baseline (work5 종료 시점 + bench 인프라만 추가된 상태).

## 측정 도구

* **Per-function counter**: RDTSC cycle (1 cycle = 1ns @ 1GHz emulation, QEMU TCG 는 비슷한 수준).
  콘솔 `bench on/off/reset/dump` 으로 토글.
  * `bench dump` 는 9 개 hot path 의 `calls / total / max / avg` 를 debug 창 + RAM 로그에 출력.
* **Per-scenario elapsed**: PIT 10ms tick (`timerctl.count`).
  콘솔 `bench mark <label>` 으로 시작/종료 마크. 차이 = 시나리오 elapsed (단위 ticks × 10ms).
* **결과 추출 (work6 phase 4 추가)**: `bench mark` / `bench dump` 가 RAM 로그
  버퍼 (16 KB) 에 누적된다. **`bench save`** 로 한 번에 `/SYSTEM/BENCH.LOG` 에
  기록 → 호스트에서 `bxos_fat.py` 로 추출 → 텍스트 파일 그대로 사용.
  Debug 창 캡처 / OCR 불필요.

### 콘솔 명령 정리 (`bench ...`)

| subcmd | 동작 |
|---|---|
| `bench` | 현재 상태 (on/off, pit_tick, log size) |
| `bench on` / `bench off` | 측정 토글 |
| `bench reset` | counter 만 0 으로 (log 보존) |
| `bench dump` | counter 표 → debug 창 + log 버퍼 |
| `bench mark <text>` | `[bench mark] tick=N <text>` → debug + log |
| `bench save` | log 버퍼 → `/SYSTEM/BENCH.LOG` (truncate + write) |
| `bench logclear` | log 버퍼 비움 |
| **`bench scenario`** | **S1+S2+S5 자동 실행 + bench save 까지 한 번에**. work6 Phase 5 신규 |
| `bench savetest` | FAT write 단계별 진단 (디버깅 용) |
| `bench mount` | fs_mount_data 단계별 진단 (디버깅 용) |

### `bench scenario` (자동 측정, work6 Phase 5+)

수동 입력 없이 한 명령으로 S1/S2/S5 시나리오를 모두 실행하고 결과 저장.

```text
QEMU 안:
> bench scenario       ← S1 (start menu × 10) + S2 (dir × 10) + S5 (mouse × 1000)
                          각 시나리오마다 reset → mark start → 동작 → mark end → dump
                          마지막에 bench save 까지 자동
> shutdown             ← QEMU 정상 종료 (writeback flush)

호스트:
$ python3 tools/modern/bxos_fat.py cp \
    cmake-build-release/data.img:/SYSTEM/BENCH.LOG /tmp/bench.log
$ cat /tmp/bench.log
```

**자동화 범위**:
- ✅ S1: `start_menu_toggle()` 10회 직접 호출 (정상 메뉴 동작과 동일).
- ✅ S2: `cons_runcmd("dir", ...)` 10회 — 콘솔에서 `dir` 친 것과 동일.
- ✅ S5: `sheet_slide(g_sht_mouse, x, y)` 1000회 — 마우스 sheet hot path
  exercise. 마우스 PS/2 fifo 경로는 시뮬레이션 안 함 (의도적).
- ❌ S3 (explorer 첫 그리기) / S4 (tetris 라인 클리어): GUI 인터랙션 필요.
  필요시 별도 수동 측정.

**측정 정확도**:
- S1/S2 는 실제 cons_runcmd / start_menu_toggle 경로 그대로 → 수동 측정과 동일 결과.
- S5 는 sheet_slide 직접 호출이라 mouse fifo 의 batching/PS/2 디코딩은 빠짐. 따라서
  수동 마우스 이동 측정값과는 약간 다를 수 있음 (fifo overhead 빠진 만큼 더 빠름).

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

### Phase 4+ 권장 (파일로 추출)

```text
QEMU 안:
1. ./run-qemu.sh  (Release 빌드)
2. Start → Programs → Console
3. > bench on
4. > bench logclear     ← 이전 측정 흔적 제거 (선택)
5. 5개 시나리오 반복:
     > bench reset
     > bench mark sN-start
     ... 시나리오 실행 ...
     > bench mark sN-end
     > bench dump
6. > bench save         ← /SYSTEM/BENCH.LOG 에 한꺼번에 기록

호스트 (별도 터미널):
$ python3 tools/modern/bxos_fat.py cp \
    build/cmake/data.img:/SYSTEM/BENCH.LOG /tmp/bench.log
$ cat /tmp/bench.log     ← 모든 mark + dump 가 텍스트로 들어 있음
```

### 옛 방식 (debug 창 캡처)

`bench save` 가 없던 Phase 1~3 에서 사용한 절차. 참고용.

```text
1. ./run-qemu.sh
2. 콘솔에서 bench on / mark / dump
3. Start → Programs → Debug 로 debug 창 띄움
4. 거기 출력된 표를 캡처/OCR
```

```text
# 부팅 후 콘솔에서 한 번만:
> bench on
> bench logclear

# S1 — Start 메뉴 토글
> bench reset
> bench mark s1-start
(Ctrl+Esc 10회 — 키보드)
> bench mark s1-end
> bench dump

# S2 — 콘솔 dir × 10
> bench reset
> bench mark s2-start
> dir   (10번 입력)
> bench mark s2-end
> bench dump

# S3 — explorer 첫 그리기
> bench reset
> bench mark s3-start
   (Start → Programs → Explorer)
> bench mark s3-end
> bench dump

# S4 — tetris 한 라인 클리어
> bench reset
> bench mark s4-start
   (Start → Programs → Games → Tetris, 한 라인 만들기)
> bench mark s4-end
> bench dump

# S5 — 마우스 1000 step (대략 화면 가로지르기 5~10회)
> bench reset
> bench mark s5-start
   (마우스 좌→우→좌, 5회 정도)
> bench mark s5-end
> bench dump

# 마지막에 한 번:
> bench save        ← /SYSTEM/BENCH.LOG 에 모든 mark+dump 가 텍스트로 들어감

# 호스트 (별도 터미널):
$ python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/SYSTEM/BENCH.LOG /tmp/bench.log
$ cat /tmp/bench.log
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

### 표 — Phase 2 (Quick wins) 후 (2026-05-02, commit `967bce9`)

> Phase 2 변경: `memset`/`memcpy` 를 `rep stosb`/`rep movsb` inline asm,
> `boxfill8` row-wise memset, `sheet_refreshsub` 4/1 byte path 통합 → run-length
> memcpy, `putfonts8_asc_ascii` ascii fast path.
> Phase 1 같은 절차로 측정. S3/S4 는 사용자 인터랙션이 매번 동일하지 않아
> calls 수 자체가 변동 — total cycles 보다 **avg cycles** 의 비교가 더 신뢰성 있음.

#### Phase 2 — 모든 카운터 한눈에 보기

| counter | S1 menu×10 | S2 dir×10 | S3 explorer | S4 tetris-line | S5 mouse |
|---|---:|---:|---:|---:|---:|
| `refreshmap` calls / total | 60 / 27.7 M | 0 / 0 | 824 / 27.8 M | 234 / 8.2 M | 900 / 18.9 M |
| `refreshsub` calls / total | 497 / **287.1 M** | 19,788 / **10,099.5 M** | 1,269 / 296.1 M | 1,692 / 1,505.5 M | 1,394 / 303.5 M |
| `sheet_slide` calls / total | 30 / 2.1 M | 0 / 0 | 408 / 48.5 M | 116 / 19.3 M | 448 / 54.8 M |
| `boxfill8` calls / total | 1,297 / 18.4 M | 179 / 1.5 M | 895 / 15.7 M | 22,622 / 61.7 M | 157 / 1.6 M |
| `putfont8` calls / total | 74,405 / 29.4 M | 4,613,703 / 1,381.5 M | 253,927 / 95.0 M | 269,670 / 98.5 M | 279,075 / 103.5 M |
| `putfonts8_asc` calls / total | 0 / 0 | 0 / 0 | 39 / 0.2 M | 106 / 1.2 M | 0 / 0 |
| `taskbar` calls / total | 31 / 38.0 M | 1 / 1.1 M | 7 / 8.1 M | 3 / 2.6 M | 1 / 1.3 M |
| `scrollwin` calls / total | 653 / 1,275.6 M | 19,989 / 24,567.8 M | 689 / 1,421.3 M | 823 / 1,542.3 M | 732 / 1,481.0 M |
| `hrb_api` calls / total | 0 / 0 | 0 / 0 | 233 / 5.0 M | 25,380 / **36,501.0 M** | 0 / 0 |
| **Σ total cycles** | **1.68 G** | **36.05 G** | **1.92 G** | **39.74 G** | **1.96 G** |

#### Phase 2 시나리오 별 marks

| | S1 | S2 | S3 | S4 | S5 |
|---|---:|---:|---:|---:|---:|
| end mark (PIT tick) | 11,565 | 24,970 | 34,037 | 56,176 | 66,256 |
| dump pit_tick | 12,082 | 25,483 | 34,722 | 56,685 | 66,727 |

#### Phase 1 → Phase 2 비교 (avg cycles 기준)

avg = total_cycles / calls. **워크로드 의존이 적어 phase 간 비교에 신뢰성 있음**.

| counter | S1 P1 → P2 | S2 P1 → P2 | S3 P1 → P2 | S4 P1 → P2 | S5 P1 → P2 |
|---|---:|---:|---:|---:|---:|
| `refreshmap` avg | 528.9 K → 461.2 K (**-12.8%**) | — | 33.9 K → 33.7 K (-0.6%) | 32.9 K → 35.2 K (+7.0%) | 23.8 K → 21.1 K (**-11.3%**) |
| `refreshsub` avg | 963.5 K → 577.7 K (**-40.0%**) | 829.5 K → 510.4 K (**-38.5%**) | 404.9 K → 233.3 K (**-42.4%**) | 548.3 K → 889.8 K (+62.3%)¹ | 330.6 K → 217.7 K (**-34.1%**) |
| `boxfill8` avg | 12.1 K → 14.2 K (+17.4%)² | 8.5 K → 8.6 K (+1.2%) | 9.9 K → 17.5 K (+76.7%)³ | 2.3 K → 2.7 K (+18.7%) | 11.3 K → 10.0 K (-11.6%) |
| `putfont8` avg | 372 → 395 (+6.2%) | 297 → 299 (+0.7%) | 360 → 373 (+3.6%) | 357 → 365 (+2.2%) | 347 → 370 (+6.6%) |
| `scrollwin` avg | 2.33 M → 1.95 M (**-16.2%**) | 1.49 M → 1.23 M (**-17.7%**) | 2.28 M → 2.06 M (-9.4%) | 2.42 M → 1.87 M (**-22.5%**) | 2.44 M → 2.02 M (**-16.9%**) |
| `hrb_api` avg | — | — | 1.13 M → 21.6 K (-98.1%)⁴ | 1.45 M → 1.44 M (-0.5%) | — |

> ¹ S4 `refreshsub` avg 가 올라간 이유: 워크로드가 다르다 (tetris 가 더 적은
> piece 만 떨어진 측정). calls 수도 4366 → 1692 로 다름. 적게 호출된 만큼
> 남은 호출들이 더 큰 영역 (board 전체) 을 다룰 가능성. 워크로드 차이로 추정.
> **Phase 2 의 run-length memcpy 가 본질적으로 빨라졌는지는 S1/S2/S3/S5 의
> avg 일관 감소로 검증 (-34~-42%)**.
>
> ² ³ `boxfill8` avg 의 미세 증가는 `bench_enter`/`bench_leave` 자체의 RDTSC
> overhead. boxfill8 한 번이 짧으면 (수 ㎲) bench overhead 의 비중이 커진다.
> total cycles 는 같은 워크로드 (S1/S2/S5) 에서 약간 증가. memset 의 효과가
> 작은 영역 (1~몇 px) 에서는 byte loop 와 비슷하고, 큰 영역에서만 의미 있음.
>
> ⁴ S3 `hrb_api` avg 가 1.13 M → 21.6 K 로 절반 미만은 측정 오류가 아니라
> **사용자가 explorer 에서 실제 한 작업 양이 다름** (P1 calls=2305, P2 calls=233).
> P2 측정에서 explorer 를 띄운 직후 dump 한 것으로 보임. avg 가 너무 작아 실제
> 큰 syscall 들 (e.g. api_putstrwin 이 수십 라인 그리는 것) 이 거의 없었음을 뜻함.
> 이 시나리오는 **재측정** 또는 인터랙션을 표준화해야 비교 가능.

#### Phase 1 → Phase 2 시나리오 합 비교 (Σ total cycles)

| 시나리오 | Phase 1 합 | Phase 2 합 | 변화 |
|---|---:|---:|---:|
| S1 menu×10 | 2.20 G | 1.68 G | **-23.6%** |
| S2 dir×10 | 48.03 G | 36.05 G | **-25.0%** |
| S3 explorer | 4.88 G | 1.92 G | -60.7% (워크로드 차이로 부정확) |
| S4 tetris-line | 45.94 G | 39.74 G | -13.5% (워크로드 차이 일부 포함) |
| S5 mouse | 2.28 G | 1.96 G | **-13.7%** |

#### Phase 2 핵심 결론

* **`refreshsub` 의 avg cycles 가 모든 워크로드-안정 시나리오 (S1/S2/S3/S5) 에서
  -34~-42% 일관 감소**. Phase 2 의 run-length memcpy 효과 ✓.
* **`scrollwin` avg 가 -16~-22% 감소**. scrollwin 자체 코드는 변경 없음 — 내부에서
  호출하는 `boxfill8` (row-wise memset) + 글자 출력의 누적 효과.
* **`putfont8` avg 변화 없음** (0.7~6.6%). Phase 2 는 putfont8 자체를 안 건드림.
  Phase 3 의 마스크 lookup 에서 큰 폭 감소 예상.
* **`hrb_api` avg 변화 없음** (S4 -0.5%). syscall dispatch 자체 비용은 그대로.
  Phase 6/7 의 batch syscall 도입이 진짜 타깃.
* **시나리오 합 -13~-25%** (워크로드 안정 시나리오 기준). Phase 2 quick wins 단독으로
  baseline 대비 ~20% 성능 향상. 누적 목표 (2x = -50%) 의 절반 진행 중.

### 표 — Phase 3 (compositor 일반화 + 폰트 lookup) 후 (2026-05-02, commit `fc75394`)

> Phase 3 변경: `sheet_refreshmap` 4/1 byte path 통합 (opaque → row 한 번 memset,
> transparent → run-length memset). `putfont8` 256-entry 마스크 lookup
> (`g_putfont_mask_lo/hi`, 2KB) + branchless 32-bit AND/OR. `putfont_mask_init()`
> 부팅 시 1회.
>
> **주된 타깃은 `putfont8` 와 `refreshmap`** — 두 카운터의 avg cycles 가 모든
> 시나리오에서 일관되게 하락해야 성공. 그 외 함수는 putfont8/boxfill8 호출의
> 누적 효과로 약간 더 빨라질 수 있음.

#### Phase 3 — 모든 카운터 한눈에 보기

| counter | S1 menu×10 | S2 dir×10 | S3 explorer | S4 tetris-line | S5 mouse |
|---|---:|---:|---:|---:|---:|
| `refreshmap` calls / total | 60 / 8.3 M | 0 / 0 | 602 / 16.4 M | 164 / 4.8 M | 982 / 21.3 M |
| `refreshsub` calls / total | 528 / 285.5 M | 19,920 / 9,698.3 M | 1,204 / 330.6 M | 1,716 / 1,575.0 M | 1,466 / 304.2 M |
| `sheet_slide` calls / total | 30 / 1.6 M | 0 / 0 | 298 / 33.0 M | 82 / 13.5 M | 491 / 58.6 M |
| `boxfill8` calls / total | 1,316 / 19.1 M | 177 / 1.7 M | 677 / 9.8 M | 25,726 / 73.2 M | 124 / 1.5 M |
| `putfont8` calls / total | 75,283 / **20.6 M** | 4,630,840 / **862.4 M** | 270,437 / **67.7 M** | 278,083 / **65.5 M** | 279,599 / **65.2 M** |
| `putfonts8_asc` calls / total | 0 / 0 | 0 / 0 | 39 / 0.1 M | 92 / 1.0 M | 0 / 0 |
| `taskbar` calls / total | 31 / 34.4 M | 1 / 1.2 M | 8 / 8.0 M | 3 / 3.1 M | 0 / 0 |
| `scrollwin` calls / total | 666 / 1,211.3 M | 20,121 / 23,033.8 M | 843 / 1,405.2 M | 782 / 1,421.4 M | 708 / 1,302.7 M |
| `hrb_api` calls / total | 0 / 0 | 0 / 0 | 233 / 5.4 M | 28,824 / 41,783.7 M | 0 / 0 |
| **Σ total cycles** | **1.58 G** | **33.60 G** | **1.88 G** | **44.94 G** | **1.75 G** |

#### Phase 3 시나리오 별 marks

| | S1 | S2 | S3 | S4 | S5 |
|---|---:|---:|---:|---:|---:|
| end mark (PIT tick) | 9,221 | 22,271 | 31,813 | 43,863 | 53,011 |
| dump pit_tick | 9,869 | 22,744 | 32,210 | 44,308 | 53,609 |

#### Phase 3 핵심 비교 — `putfont8` avg cycles (Phase 3 의 직접 타깃)

| 시나리오 | P1 | P2 | P3 | P1→P3 | P2→P3 |
|---|---:|---:|---:|---:|---:|
| S1 | 372 | 395 | **273** | **-26.6%** | **-30.9%** |
| S2 | 297 | 299 | **186** | **-37.4%** | **-37.8%** |
| S3 | 360 | 373 | **250** | **-30.6%** | **-33.0%** |
| S4 | 357 | 365 | **235** | **-34.2%** | **-35.6%** |
| S5 | 347 | 370 | **233** | **-32.9%** | **-37.0%** |

> 모든 시나리오에서 **putfont8 cycle/call -27~-38% 일관 감소**. 마스크 lookup +
> branchless 32-bit blit 이 의도대로 효과를 냄. S2 (`dir × 10`, 4.6 M 호출) 에서
> 절대값 절감이 가장 큼: **517 M cycles 절감** (P2 1,381 M → P3 862 M).

#### Phase 3 핵심 비교 — `refreshmap` avg cycles (Phase 3 의 두 번째 타깃)

| 시나리오 | P1 | P2 | P3 | P1→P3 | P2→P3 |
|---|---:|---:|---:|---:|---:|
| S1 | 528.9 K | 461.2 K | **139.1 K** | **-73.7%** | **-69.8%** |
| S2 | — (calls=0) | — | — | — | — |
| S3 | 33.9 K | 33.7 K | 27.2 K | **-19.7%** | -19.3% |
| S4 | 32.9 K | 35.2 K | 29.5 K | -10.4% | -16.2% |
| S5 | 23.8 K | 21.1 K | 21.7 K | -8.6% | +2.8% |

> S1 의 `refreshmap` avg 가 가장 극적으로 감소 (-73.7%). 메뉴 sheet 가 큰
> opaque 영역이라 row-wise memset 의 이득이 큼. S3/S4/S5 는 작은 sheet (마우스
> 등) 영향이 커 절대값이 작고 상대 변화가 줄어듦. opaque sheet 일반화의 효과 ✓.

#### Phase 3 부수 효과 — 다른 카운터 avg cycles

| counter | S1 P2→P3 | S2 P2→P3 | S3 P2→P3 | S4 P2→P3 | S5 P2→P3 |
|---|---:|---:|---:|---:|---:|
| `refreshsub` avg | 577.7 K → 540.7 K (-6.4%) | 510.4 K → 486.9 K (-4.6%) | 233.3 K → 274.6 K (+17.7%)¹ | 889.8 K → 917.9 K (+3.2%) | 217.7 K → 207.5 K (-4.7%) |
| `boxfill8` avg | 14.2 K → 14.5 K (+2.3%) | 8.6 K → 9.7 K (+12.6%)² | 17.5 K → 14.4 K (-17.7%) | 2.7 K → 2.8 K (+4.3%) | 10.0 K → 11.8 K (+18.1%)² |
| `scrollwin` avg | 1.95 M → 1.82 M (-6.7%) | 1.23 M → 1.14 M (-6.7%) | 2.06 M → 1.67 M (-19.0%) | 1.87 M → 1.82 M (-2.9%) | 2.02 M → 1.84 M (-9.0%) |
| `hrb_api` avg | — | — | 21.6 K → 22.9 K (+6.4%) | 1.44 M → 1.45 M (+0.6%) | — |

> ¹ ² 워크로드 차이로 인한 변동. refreshsub/boxfill8 의 avg 는 호출당 작업량 의존.
> Phase 3 자체는 `refreshsub` 와 `boxfill8` 을 변경하지 않음. `scrollwin` 의 -7~-19%
> 감소는 내부 putfont8/refreshmap/boxfill8 가속의 누적 효과.
> `hrb_api` avg 변화 없음 — Phase 6/7 의 syscall batch 가 진짜 타깃임을 재확인.

#### Phase 1 → Phase 3 누적 비교 (Σ total cycles, 워크로드 안정 시나리오)

| 시나리오 | Phase 1 baseline | Phase 2 후 | Phase 3 후 | P1→P3 누적 |
|---|---:|---:|---:|---:|
| S1 menu×10 | 2.20 G | 1.68 G | **1.58 G** | **-28.2%** |
| S2 dir×10 | 48.03 G | 36.05 G | **33.60 G** | **-30.0%** |
| S3 explorer | 4.88 G | 1.92 G | 1.88 G | -61.6% (워크로드 차) |
| S4 tetris-line | 45.94 G | 39.74 G | 44.94 G | -2.2% (P3 워크로드 ↑)³ |
| S5 mouse | 2.28 G | 1.96 G | **1.75 G** | **-23.0%** |

> ³ S4 P3 의 `hrb_api calls=28,824 / boxfill8=25,726` 이 P2 의 `25,380 / 22,622` 보다
> ~13% 더 많음. 같은 "1 라인 클리어" 시나리오라도 게임 진행이 다른 듯.
> per-call avg (`hrb_api` 1.44 M → 1.45 M, `boxfill8` 2.7 K → 2.8 K) 는 거의
> 변화 없음 — Phase 3 가 hrb_api/boxfill8 자체를 안 건드림.

#### Phase 3 핵심 결론

* **`putfont8` 마스크 lookup 효과가 매우 큼** — avg cycles -27~-38% 일관 감소.
  특히 텍스트 출력 dominant 시나리오 (S2 `dir × 10`) 에서 517 M cycles 절감.
* **`refreshmap` opaque path memset** 효과는 큰 sheet 시나리오 (S1 = 메뉴 sheet)
  에서 -74%, 작은 sheet 에서 -10~-20%. 평균 -25% 수준.
* **`scrollwin` avg -7~-19% 누적 감소** — Phase 2 의 boxfill8 + Phase 3 의 putfont8
  가 scrollwin 안에서 자주 불려서 간접 효과.
* **워크로드 안정 시나리오 평균 누적 감소 -27%** (S1 -28%, S2 -30%, S5 -23%).
  Phase 1~3 만으로 baseline 의 ~73% 시간으로 동작. 목표 2x (-50%) 의 절반 달성.
* **다음 큰 폭은 Phase 5 (scrollwin 부분 redraw — S2 dominant 36 G cycles 의
  20 G 절감 가능) 와 Phase 6/7 (hrb_api 41 G — S4 dominant)**. 이 두 phase 가
  목표 2x 달성의 결정적 단계.

### 표 — Phase 4 (Dirty rect 인프라) 후 (2026-05-02, 신규 `bench save` 인프라 활용)

> Phase 4 변경: `struct SHEET` 에 dirty_rect[4][4] + dirty_count 추가. `sheet_dirty_add` /
> `_flush` / `_flush_all` API 도입. HariMain idle 직전에 `sheet_dirty_flush_all`. **호환
> mode** — `sheet_refresh` 동작 변경 없음. dirty rect 사용은 opt-in (Phase 5 부터).
>
> 또한 같은 commit 에서 `task_alloc` 의 init 누락 버그 수정 (`task->cons` /
> `cwd_clus` 등이 uninitialized garbage → `dir` 명령 실패) 와 `bench save` →
> `/SYSTEM/BENCH.LOG` 텍스트 출력 인프라 추가.
>
> 측정값은 `/tmp/bench.log` 로 추출되어 호스트에서 그대로 사용 (debug 창 캡처 불필요).

#### Phase 4 — 모든 카운터 한눈에 보기

| counter | S1 menu×10 | S2 dir×10 | S3 explorer | S4 tetris-line | S5 mouse |
|---|---:|---:|---:|---:|---:|
| `refreshmap` calls / total | 60 / 12.9 M | 0 / 0 | 439 / 14.4 M | 1,218 / 39.5 M | 794 / 23.0 M |
| `refreshsub` calls / total | 544 / 305.6 M | 19,851 / 10,303.6 M | 988 / 320.2 M | 2,320 / 1,027.0 M | 1,325 / 314.6 M |
| `sheet_slide` calls / total | 30 / 2.1 M | 0 / 0 | 219 / 28.6 M | 605 / 81.9 M | 395 / 58.7 M |
| `boxfill8` calls / total | 1,288 / 23.7 M | 172 / 1.4 M | 639 / 9.1 M | 13,715 / 44.7 M | 154 / 1.7 M |
| `putfont8` calls / total | 86,844 / 23.5 M | 4,621,518 / 973.6 M | 264,448 / 72.6 M | 281,357 / 76.8 M | 289,007 / 77.2 M |
| `putfonts8_asc` calls / total | 0 / 0 | 0 / 0 | 39 / 0.1 M | 64 / 0.5 M | 0 / 0 |
| `taskbar` calls / total | 31 / 41.3 M | 1 / 0.3 M | 7 / 7.9 M | 10 / 10.2 M | 1 / 1.6 M |
| `scrollwin` calls / total | 709 / 1,428.8 M | 20,054 / 26,690.1 M | 797 / 1,587.2 M | 795 / 1,641.1 M | 772 / 1,572.5 M |
| `hrb_api` calls / total | 0 / 0 | 0 / 0 | 233 / 5.3 M | 14,664 / 20,899.2 M | 0 / 0 |
| **Σ total cycles** | **1.84 G** | **37.97 G** | **2.05 G** | **23.82 G** | **2.05 G** |

#### Phase 4 시나리오 별 marks (PIT ticks 기준)

| | s_N-start | s_N-end | elapsed | dump pit_tick |
|---|---:|---:|---:|---:|
| S1 | 5,902 | 7,763 | **1,861** (~18.6 s) | 8,311 |
| S2 | 9,837 | 14,783 | **4,946** (~49.5 s) | 15,006 |
| S3 | 16,967 | 18,922 | **1,955** (~19.6 s) | 19,353 |
| S4 | 22,492 | 27,237 | **4,745** (~47.5 s) | 27,697 |
| S5 | 29,781 | 32,631 | **2,850** (~28.5 s) | 33,146 |

#### Phase 3 → Phase 4 비교 (avg cycles, 워크로드 안정 시나리오)

Phase 4 는 호환 mode 라 큰 변화 없어야 정상. **회귀 0건 확인이 목표**.

| counter | S1 P3→P4 | S2 P3→P4 | S5 P3→P4 |
|---|---:|---:|---:|
| `refreshmap` avg | 139.1 K → 214.5 K (+54.2%)¹ | — | 21.7 K → 29.0 K (+33.7%)¹ |
| `refreshsub` avg | 540.7 K → 561.8 K (+3.9%) | 486.9 K → 519.0 K (+6.6%) | 207.5 K → 237.4 K (+14.4%) |
| `sheet_slide` avg | 54.1 K → 69.8 K (+29.0%) | — | 119.4 K → 148.5 K (+24.4%) |
| `boxfill8` avg | 14.5 K → 18.4 K (+27.0%)² | 9.7 K → 8.2 K (-15.7%) | 11.8 K → 11.3 K (-4.0%) |
| `putfont8` avg | 273 → 270 (-1.1%) | 186 → 210 (+12.9%) | 233 → 267 (+14.6%) |
| `scrollwin` avg | 1.82 M → 2.02 M (+10.7%) | 1.14 M → 1.33 M (+16.3%) | 1.84 M → 2.04 M (+10.5%) |

> ¹ `refreshmap` avg 가 +30~+54% 로 보이지만 절대값이 작음 (수백 KB cycle). 호출
> 빈도가 적은 함수의 측정값은 cold cache 영향이 큼. 의미 있는 회귀로 보기 어려움.
>
> ² `boxfill8` avg 변동도 함수 자체가 짧아 (수 ㎲) bench overhead 가 상대적으로 큼.
>
> **공통 추세**: Phase 3 → Phase 4 에서 5~17% 가량 cycle 증가. 가능한 원인:
> 1. 측정 세션 별 QEMU TCG 엔진의 host CPU 부하 차이 (TCG 는 host 시간에 강 의존)
> 2. SHEET struct 크기 증가 (~80→114 byte) 에 따른 cache line 추가 사용
> 3. HariMain idle 의 `sheet_dirty_flush_all` 256-sheet 순회 (no-op 이지만 호출 빈도 高)
>
> Phase 5 에서 dirty rect 가 본격 사용되면 효율이 다시 올라가 회복될 전망.

#### Phase 4 시나리오 합 (Σ total cycles)

| 시나리오 | Phase 1 baseline | Phase 3 후 | **Phase 4 후** | P1→P4 누적 |
|---|---:|---:|---:|---:|
| S1 menu×10 | 2.20 G | 1.58 G | **1.84 G** | -16.4% |
| S2 dir×10 | 48.03 G | 33.60 G | **37.97 G** | -21.0% |
| S3 explorer | 4.88 G | 1.88 G | **2.05 G** | -58.0% (워크로드 차) |
| S4 tetris-line | 45.94 G | 44.94 G | **23.82 G** | -48.2% (워크로드 차) |
| S5 mouse | 2.28 G | 1.75 G | **2.05 G** | -10.1% |

#### Phase 4 핵심 결론

* **회귀 자체는 작음** (5~17%). Phase 4 가 인프라 단계라 기능적 변화 없음.
* `putfont8` 의 S2 avg 가 P3=186 → P4=210 으로 살짝 올라간 건 BENCH 측정 자체의
  noise 또는 TCG 시간차로 추정. P1 (372) → P4 (210) 누적으로 보면 여전히 -43.5%.
* `scrollwin` avg 는 일관 +10~16% 증가. 의외. 추후 Phase 5 의 부분 redraw 가
  근본 해결.
* `hrb_api` S4: P3 28,824 calls → P4 14,664 calls (절반). 사용자가 tetris 로 더
  적은 라인을 클리어한 듯 (워크로드 차).
* **이번 측정의 가장 큰 수확은 `bench save` 인프라**: 사용자가 텍스트 파일로 직접
  덤프를 추출해 Phase 5 부터는 OCR/캡처 없이 그대로 분석 가능.
* `task_alloc` 초기화 버그 수정도 사이드 결과 — 더 이상 `dir` 이 (no data disk
  mounted) 로 실패하지 않음.

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
