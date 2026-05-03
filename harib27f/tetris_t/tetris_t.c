/* tetris_t.c — work6 Phase 5+ bench scenario S4 자동화 앱.
 *
 * tetris.c 의 line-clear / redraw_field 핫패스를 N 회 반복해 보드 redraw
 * 비용을 측정한다. 사용자 인터랙션 없음. 일정 회수 후 자체 종료.
 *
 * Redraw 경로 3종 — `BENCH_MODE` 매크로로 선택. Phase 7 기준 default = 2 (blit_rect).
 *   0 = api_boxfilwin (cell × 200) + api_refreshwin 1회
 *   1 = api_boxfilwin (cell × 200) + api_invalidate_rect (cell × 200) + api_dirty_flush 1회
 *   2 = user buffer 에 직접 채우기 + api_blit_rect 1회 (Phase 7 추천)
 *
 * mode 2 가 가장 빠른 이유: cell × 200 syscall 이 buffer 직접 채우기 (fill/memset)
 * 로 변환되고, blit_rect 가 row 단위 memcpy 로 winbuf 에 한 번에 옮기기 때문.
 * Phase 6 측정에서 mode 1 이 mode 0 보다 +10% 느렸던 원인 (invalidate_rect 200회
 * syscall overhead) 을 제거.
 */

/* 0=refreshwin / 1=invalidate+flush / 2=blit_rect (Phase 7 default). */
#define BENCH_MODE   2

#include "apilib.h"

#define FW          10
#define FH          20
#define CELL        12
#define FX0         8
#define FY0         30
#define BOARD_W     (FW * CELL)
#define BOARD_H     (FH * CELL)
#define WIN_W       (FX0 + BOARD_W + 70)
#define WIN_H       (FY0 + BOARD_H + 12)

#define ITERATIONS   5

static unsigned char field[FH][FW];

/* mode 2 전용 — 보드 픽셀 buffer (BOARD_W × BOARD_H = 28,800 byte). */
#if BENCH_MODE == 2
static unsigned char board_buf[BOARD_W * BOARD_H];
#endif

static const unsigned char piece_color[7] = { 6, 3, 5, 2, 1, 33, 216 };

#if BENCH_MODE == 2

/* mode 2: cell 을 board_buf 에 직접 채우기. syscall 없음.
 * border (0) + interior (col) — putstrwin 의 작은 boxfill 4 + 1 동일 시각. */
static void draw_cell(int cx, int cy, int col)
{
	int x0 = cx * CELL, y0 = cy * CELL;
	int x, y;
	unsigned char c = (unsigned char) col;
	for (y = 0; y < CELL; y++) {
		unsigned char *row = board_buf + (y0 + y) * BOARD_W + x0;
		int border_y = (col != 0) && (y == 0 || y == CELL - 1);
		for (x = 0; x < CELL; x++) {
			int border_x = (col != 0) && (x == 0 || x == CELL - 1);
			row[x] = (border_y || border_x) ? 0 : c;
		}
	}
}

static void redraw_field(int win)
{
	(void) win;
	int x, y;
	for (y = 0; y < FH; y++)
		for (x = 0; x < FW; x++)
			draw_cell(x, y, field[y][x]);
}

static void refresh_field(int win)
{
	api_blit_rect(win, board_buf, BOARD_W, BOARD_H, FX0, FY0);
}

#else  /* BENCH_MODE 0 or 1 — boxfilwin per cell */

static void draw_cell(int win, int cx, int cy, int col)
{
	int x0 = FX0 + cx * CELL;
	int y0 = FY0 + cy * CELL;
	int x1 = x0 + CELL - 1;
	int y1 = y0 + CELL - 1;
	api_boxfilwin(win + 1, x0, y0, x1, y1, col);
	if (col != 0) {
		api_boxfilwin(win + 1, x0, y0, x1, y0, 0);
		api_boxfilwin(win + 1, x0, y1, x1, y1, 0);
		api_boxfilwin(win + 1, x0, y0, x0, y1, 0);
		api_boxfilwin(win + 1, x1, y0, x1, y1, 0);
	}
#if BENCH_MODE == 1
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
#if BENCH_MODE == 1
	api_dirty_flush(win);
#else
	api_refreshwin(win, FX0, FY0, FX0 + BOARD_W, FY0 + BOARD_H);
#endif
}

#endif

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
			FX0 + BOARD_W, FY0 + BOARD_H, 0);

	for (i = 0; i < FH * FW; i++) ((unsigned char *) field)[i] = 0;
	redraw_field(win);
	refresh_field(win);

	for (i = 0; i < ITERATIONS; i++) {
		for (y = 0; y < FH - 11; y++)
			for (x = 0; x < FW; x++) field[y][x] = 0;
		for (y = FH - 11; y < FH - 1; y++)
			for (x = 0; x < FW; x++)
				field[y][x] = ((x + y) & 3) ? piece_color[(x + i) % 7] : 0;
		for (x = 0; x < FW; x++)
			field[FH - 1][x] = piece_color[(x + i) % 7];

		redraw_field(win);
		refresh_field(win);

		clear_lines();
		redraw_field(win);
		refresh_field(win);
	}

	api_putstrwin(win, FX0 + 16, FY0 + BOARD_H / 2 - 4,
			7, 6, "DONE T");
	api_refreshwin(win,
			FX0 + 16, FY0 + BOARD_H / 2 - 4,
			FX0 + 16 + 6 * 8, FY0 + BOARD_H / 2 - 4 + 16);

	api_end();
}
