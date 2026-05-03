/* tetris_t.c — work6 Phase 5 bench scenario S4 자동화 앱.
 *
 * tetris.c 의 line-clear / redraw_field 핫패스를 N 회 반복해 보드 redraw
 * 비용을 측정한다. 사용자 인터랙션 없음. 일정 회수 후 자체 종료.
 *
 * Phase 6 부터: 신규 syscall (`api_blit_rect`, `api_invalidate_rect`,
 * `api_dirty_flush`) 의 효과를 비교 측정하기 위해 redraw 경로를 두 종류 제공.
 * 기본은 새 batch 경로 (USE_PHASE6_BATCH=1). 0 으로 바꾸면 기존
 * boxfilwin × N + refreshwin 1회 경로.
 *
 * 자동 측정 절차 (`bench scenario` 가 launch):
 *   1. 윈도우 열고 빈 보드 1회 그리기
 *   2. ITERATIONS 회 반복:
 *      a. 보드 셋업 (윗 row 패턴, 마지막 row 가득)
 *      b. redraw_field — 모든 셀 그리기 (deferred)
 *      c. flush — 누적된 dirty 한 번에 refresh
 *      d. clear_lines() — 마지막 row 제거 + shift
 *      e. 다시 redraw + flush
 *   3. api_end() 로 종료
 */

/* 1 = 신규 syscall (api_invalidate_rect + api_dirty_flush) batch 경로.
 * 0 = 기존 api_boxfilwin + api_refreshwin 경로. */
#define USE_PHASE6_BATCH  1

#include "apilib.h"

#define FW          10
#define FH          20
#define CELL        12
#define FX0         8
#define FY0         30
#define WIN_W       (FX0 + FW * CELL + 70)
#define WIN_H       (FY0 + FH * CELL + 12)

/* 반복 횟수 — 측정 시간이 너무 짧으면 noise 큼. 5회 정도면 ~5 라인 클리어
 * 분량의 redraw 작업. 너무 많으면 측정 시간 길어져 오히려 비교 어려움. */
#define ITERATIONS   5

static unsigned char field[FH][FW];

static const unsigned char piece_color[7] = { 6, 3, 5, 2, 1, 33, 216 };

static void cell_rect(int cx, int cy, int *x0, int *y0, int *x1, int *y1)
{
	*x0 = FX0 + cx * CELL;
	*y0 = FY0 + cy * CELL;
	*x1 = *x0 + CELL - 1;
	*y1 = *y0 + CELL - 1;
}

static void draw_cell(int win, int cx, int cy, int col)
{
	int x0, y0, x1, y1;
	cell_rect(cx, cy, &x0, &y0, &x1, &y1);
	/* deferred refresh — 마지막에 한 번만 flush */
	api_boxfilwin(win + 1, x0, y0, x1, y1, col);
	if (col != 0) {
		api_boxfilwin(win + 1, x0, y0, x1, y0, 0);
		api_boxfilwin(win + 1, x0, y1, x1, y1, 0);
		api_boxfilwin(win + 1, x0, y0, x0, y1, 0);
		api_boxfilwin(win + 1, x1, y0, x1, y1, 0);
	}
#if USE_PHASE6_BATCH
	/* dirty rect 만 누적 — 마지막 flush 에서 한 번에 처리 */
	api_invalidate_rect(win, x0, y0, x1 + 1, y1 + 1);
#endif
}

static void redraw_field(int win)
{
	int x, y;
	for (y = 0; y < FH; y++)
		for (x = 0; x < FW; x++)
			draw_cell(win, x, y, field[y][x]);
}

static void refresh_field(int win)
{
#if USE_PHASE6_BATCH
	/* Phase 6: 누적된 dirty rect 를 한 번에 flush. 보드 전체 = 한 union rect. */
	api_dirty_flush(win);
#else
	api_refreshwin(win, FX0, FY0, FX0 + FW * CELL, FY0 + FH * CELL);
#endif
}

static int clear_lines(void)
{
	int y, x, n = 0, full;
	for (y = FH - 1; y >= 0; ) {
		full = 1;
		for (x = 0; x < FW; x++)
			if (field[y][x] == 0) { full = 0; break; }
		if (full) {
			int yy;
			for (yy = y; yy > 0; yy--)
				for (x = 0; x < FW; x++) field[yy][x] = field[yy - 1][x];
			for (x = 0; x < FW; x++) field[0][x] = 0;
			n++;
		} else {
			y--;
		}
	}
	return n;
}

void HariMain(void)
{
	char winbuf[WIN_W * WIN_H];
	int win, i, x, y;

	win = api_openwin(winbuf, WIN_W, WIN_H, -1, "tetris_t");
	api_boxfilwin(win, 6, 27, WIN_W - 7, WIN_H - 7, 7);
	api_boxfilwin(win, FX0 - 1, FY0 - 1,
			FX0 + FW * CELL, FY0 + FH * CELL, 0);

	/* 빈 보드 한 번 그리기 (베이스라인) */
	for (i = 0; i < FH * FW; i++) ((unsigned char *) field)[i] = 0;
	redraw_field(win);
	refresh_field(win);

	for (i = 0; i < ITERATIONS; i++) {
		/* 보드 셋업: 위 9 row 일부 채움 (모두 색 깔리지만 가득 안 차게),
		 * 마지막 row 는 가득 채워서 clear_lines 가 발동되게 함. */
		for (y = 0; y < FH - 11; y++)
			for (x = 0; x < FW; x++) field[y][x] = 0;
		for (y = FH - 11; y < FH - 1; y++)
			for (x = 0; x < FW; x++)
				/* 격자 패턴 — 한 row 안에 빈 셀이 있어 가득 안 참 */
				field[y][x] = ((x + y) & 3) ? piece_color[(x + i) % 7] : 0;
		for (x = 0; x < FW; x++)
			field[FH - 1][x] = piece_color[(x + i) % 7];

		/* 보드 그리기 */
		redraw_field(win);
		refresh_field(win);

		/* line clear 트리거 + redraw — 핵심 측정 대상 */
		clear_lines();
		redraw_field(win);
		refresh_field(win);
	}

	/* 종료 라벨 (디버깅 용) — 그리고 종료 */
	api_putstrwin(win, FX0 + 16, FY0 + FH * CELL / 2 - 4,
			7, 6, "DONE T");
	api_refreshwin(win,
			FX0 + 16, FY0 + FH * CELL / 2 - 4,
			FX0 + 16 + 6 * 8, FY0 + FH * CELL / 2 - 4 + 16);

	api_end();
}
