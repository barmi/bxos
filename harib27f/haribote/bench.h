/* harib27f/haribote/bench.h — work6 Phase 1 measurement infrastructure
 *
 * Per-function 측정: RDTSC cycle counter (정밀도 ~1 cycle).
 * Per-scenario 측정: PIT 10ms tick (timerctl.count) — 호출자가 직접 차이 계산.
 *
 * 사용 패턴:
 *     void hot_function(...) {
 *         bench_enter(BENCH_FOO);
 *         ... 본체 ...
 *         bench_leave(BENCH_FOO);
 *         return;
 *     }
 *
 * 토글:
 *   g_bench_enabled == 0 일 때 bench_enter / bench_leave 가 첫 줄에서 즉시 return.
 *   콘솔의 `bench on` / `bench off` / `bench reset` / `bench dump` 로 제어.
 */
#ifndef BXOS_BENCH_H
#define BXOS_BENCH_H

/* freestanding 환경이라 stdint.h 의 일부 typedef 만 직접 정의.
 * (i686-elf-gcc 의 builtin stdint 도 쓸 수 있지만 컴파일 단계 의존을 피한다.)  */
typedef unsigned int        bx_u32;
typedef unsigned long long  bx_u64;

/* RDTSC. 단일 CPU / 단일 task 환경. QEMU TCG 도 monotonic 에뮬. */
static inline bx_u64 bench_rdtsc(void)
{
	bx_u32 lo, hi;
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((bx_u64) hi << 32) | (bx_u64) lo;
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

struct BENCH_COUNTER {
	const char *name;
	bx_u32  calls;
	bx_u64  total_cycles;
	bx_u64  max_cycles;
};

extern int                   g_bench_enabled;     /* 0 = off (hot path no-op) */
extern struct BENCH_COUNTER  g_bench_counters[BENCH_COUNT];

void bench_init(void);
void bench_set_enabled(int on);
void bench_reset(void);
void bench_dump(void);                            /* dbg_putstr0 로 표 출력 */
void bench_enter(int idx);
void bench_leave(int idx);

#endif /* BXOS_BENCH_H */
