/* harib27f/haribote/bench.c — work6 Phase 1 measurement infrastructure
 *
 * RDTSC 기반 per-function counter. 9 개 hot path slot.
 * 단일 CPU / 단일 task 환경이라 lock 불필요 (HariMain 의 main loop 안에서만
 * fired). bench_enter 의 nesting 은 16 단까지 추적.
 */

#include "bootpack.h"
#include "bench.h"

int g_bench_enabled = 0;
struct BENCH_COUNTER g_bench_counters[BENCH_COUNT];

/* nest stack: enter 마다 push, leave 마다 pop. 같은 idx 가 다시 들어오는 재귀
 * 호출은 일단 outer 만 측정 (inner 는 enter 시점에 이미 enabled 가 true 라
 * 같이 push 되지만 leave 가 정상 매칭되는 한 자체 측정값은 합산됨). */
struct BENCH_FRAME { int idx; bx_u64 t0; };
static struct BENCH_FRAME g_bench_stack[16];
static int g_bench_sp = 0;

static const char *g_bench_names[BENCH_COUNT] = {
	"refreshmap",
	"refreshsub",
	"sheet_slide",
	"boxfill8",
	"putfont8",
	"putfonts8_asc",
	"taskbar",
	"scrollwin",
	"hrb_api",
};

void bench_init(void)
{
	int i;
	for (i = 0; i < BENCH_COUNT; i++) {
		g_bench_counters[i].name = g_bench_names[i];
		g_bench_counters[i].calls = 0;
		g_bench_counters[i].total_cycles = 0;
		g_bench_counters[i].max_cycles = 0;
	}
	g_bench_sp = 0;
	g_bench_enabled = 0;
	return;
}

void bench_set_enabled(int on)
{
	g_bench_enabled = on ? 1 : 0;
	g_bench_sp = 0;	/* nest stack reset — 토글 경계의 mismatch 방지 */
	return;
}

void bench_reset(void)
{
	int i;
	for (i = 0; i < BENCH_COUNT; i++) {
		g_bench_counters[i].calls = 0;
		g_bench_counters[i].total_cycles = 0;
		g_bench_counters[i].max_cycles = 0;
	}
	g_bench_sp = 0;
	return;
}

void bench_enter(int idx)
{
	if (!g_bench_enabled) return;
	if (idx < 0 || idx >= BENCH_COUNT) return;
	if (g_bench_sp >= (int)(sizeof g_bench_stack / sizeof g_bench_stack[0])) return;
	g_bench_stack[g_bench_sp].idx = idx;
	g_bench_stack[g_bench_sp].t0 = bench_rdtsc();
	g_bench_sp++;
	return;
}

void bench_leave(int idx)
{
	bx_u64 t1, dt;
	struct BENCH_COUNTER *c;
	if (!g_bench_enabled) return;
	if (g_bench_sp <= 0) return;
	g_bench_sp--;
	if (g_bench_stack[g_bench_sp].idx != idx) {
		/* mismatched — nest stack 비정상. 그냥 drop. */
		return;
	}
	t1 = bench_rdtsc();
	dt = t1 - g_bench_stack[g_bench_sp].t0;
	c = &g_bench_counters[idx];
	c->calls++;
	c->total_cycles += dt;
	if (dt > c->max_cycles) {
		c->max_cycles = dt;
	}
	return;
}

/* libgcc 의 ___udivdi3 / ___umoddi3 를 링크하지 않으므로 64-bit 나눗셈을
 * 32-bit 연산만으로 풀어준다. den 은 항상 32-bit. 반환 = num / den (64-bit),
 * *out_rem 에 나머지 (32-bit). */
static bx_u64 bench_div_u64_u32(bx_u64 num, bx_u32 den, bx_u32 *out_rem)
{
	bx_u32 hi = (bx_u32)(num >> 32);
	bx_u32 lo = (bx_u32) num;
	bx_u32 q_hi, q_lo, r;
	int i;
	if (den == 0) {
		if (out_rem != 0) *out_rem = 0;
		return 0;
	}
	q_hi = hi / den;
	r    = hi - q_hi * den;
	q_lo = 0;
	for (i = 31; i >= 0; i--) {
		r = (r << 1) | ((lo >> i) & 1u);
		q_lo <<= 1;
		if (r >= den) {
			r -= den;
			q_lo |= 1u;
		}
	}
	if (out_rem != 0) *out_rem = r;
	return ((bx_u64) q_hi << 32) | q_lo;
}

/* 64-bit unsigned 를 십진수 문자열로. 결과는 buf 에. 반환=문자수. */
static int bench_u64_to_dec(bx_u64 v, char *buf)
{
	char tmp[24];
	int n = 0, i;
	if (v == 0) {
		buf[0] = '0';
		buf[1] = 0;
		return 1;
	}
	while (v > 0) {
		bx_u32 rem;
		v = bench_div_u64_u32(v, 10, &rem);
		tmp[n++] = '0' + (int) rem;
	}
	for (i = 0; i < n; i++) {
		buf[i] = tmp[n - 1 - i];
	}
	buf[n] = 0;
	return n;
}

/* 한 줄 형식: "name              calls=NNN total=NNN max=NNN avg=NNN\n" */
static void bench_dump_one(struct BENCH_COUNTER *c)
{
	char line[128];
	char numbuf[24];
	int p = 0, i;
	int name_len = 0;
	while (c->name[name_len] != 0) name_len++;
	/* 이름 + 패딩 */
	for (i = 0; i < name_len && p < 16; i++) line[p++] = c->name[i];
	while (p < 16) line[p++] = ' ';

	{
		const char *lab = " calls=";
		while (*lab) line[p++] = *lab++;
	}
	bench_u64_to_dec((bx_u64) c->calls, numbuf);
	for (i = 0; numbuf[i] != 0; i++) line[p++] = numbuf[i];

	{
		const char *lab = " total=";
		while (*lab) line[p++] = *lab++;
	}
	bench_u64_to_dec(c->total_cycles, numbuf);
	for (i = 0; numbuf[i] != 0; i++) line[p++] = numbuf[i];

	{
		const char *lab = " max=";
		while (*lab) line[p++] = *lab++;
	}
	bench_u64_to_dec(c->max_cycles, numbuf);
	for (i = 0; numbuf[i] != 0; i++) line[p++] = numbuf[i];

	{
		const char *lab = " avg=";
		bx_u64 avg = 0;
		bx_u32 unused;
		if (c->calls > 0) {
			avg = bench_div_u64_u32(c->total_cycles, c->calls, &unused);
		}
		while (*lab) line[p++] = *lab++;
		bench_u64_to_dec(avg, numbuf);
		for (i = 0; numbuf[i] != 0; i++) line[p++] = numbuf[i];
	}

	line[p++] = '\n';
	line[p] = 0;
	dbg_putstr0(line, COL8_FFFFFF);
	return;
}

void bench_dump(void)
{
	char line[64];
	int i, p = 0;
	const char *header;

	header = g_bench_enabled
		? "[bench] dump (enabled)\n"
		: "[bench] dump (disabled — old data)\n";
	dbg_putstr0((char *)header, COL8_FFFF00);

	/* PIT tick 도 함께 보여줘 scenario timing 참고 */
	{
		const char *lab = "  pit_tick=";
		bx_u64 v = (bx_u64) timerctl.count;
		char numbuf[24];
		while (*lab) line[p++] = *lab++;
		bench_u64_to_dec(v, numbuf);
		for (i = 0; numbuf[i] != 0; i++) line[p++] = numbuf[i];
		line[p++] = '\n';
		line[p] = 0;
		dbg_putstr0(line, COL8_FFFFFF);
	}

	for (i = 0; i < BENCH_COUNT; i++) {
		bench_dump_one(&g_bench_counters[i]);
	}
	return;
}
