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

## 표 — Phase 1 baseline (2026-05-01)

> Phase 1 의 측정 인프라 자체 외 다른 변경이 없는 commit 시점. RDTSC cycles 단위.
> QEMU TCG / Apple Silicon M-series host. CPU emulation rate 는 QEMU 측정값에 의존.
> 절대값은 호스트마다 다를 수 있어, 다음 phase 와 **상대 비교** 만이 의미 있음.

### S1 — Start 메뉴 토글 10회

| 항목 | baseline |
|---|---|
| PIT ticks (elapsed) | (실측 후 채움) |
| `taskbar` calls / total / max | (실측) |
| `refreshmap` calls / total | (실측) |
| `refreshsub` calls / total | (실측) |
| `putfont8` calls | (실측) |

### S2 — 콘솔 `dir` × 10

| 항목 | baseline |
|---|---|
| PIT ticks (elapsed) | (실측) |
| `scrollwin` calls / total / max | (실측) |
| `boxfill8` calls / total | (실측) |
| `putfont8` calls / total | (실측) |
| `putfonts8_asc` calls / total | (실측) |
| `hrb_api` calls / total | (실측) |

### S3 — explorer `/` 첫 그리기

| 항목 | baseline |
|---|---|
| PIT ticks (elapsed) | (실측) |
| `refreshmap` calls / total | (실측) |
| `refreshsub` calls / total | (실측) |
| `putfonts8_asc` calls / total | (실측) |
| `boxfill8` calls / total | (실측) |
| `hrb_api` calls / total | (실측) |

### S4 — tetris 1 라인 클리어

| 항목 | baseline |
|---|---|
| PIT ticks (elapsed) | (실측) |
| `boxfill8` calls / total | (실측) |
| `refreshsub` calls / total | (실측) |
| `hrb_api` calls / total | (실측) |

### S5 — 마우스 1000 step

| 항목 | baseline |
|---|---|
| PIT ticks (elapsed) | (실측) |
| `sheet_slide` calls / total | (실측) |
| `refreshmap` calls / total | (실측) |
| `refreshsub` calls / total | (실측) |

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
