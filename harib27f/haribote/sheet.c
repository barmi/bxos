/* 마우스나 윈도우의 중첩 처리 */

#include "bootpack.h"
#include "bench.h"
#include <string.h>	/* memcpy / memset — work6 Phase 2 hot path */

/* 비트값은 bootpack.h 의 SHEET_FLAG_* 와 동일. 구버전 로컬 매크로도 유지.  */
#define SHEET_USE			SHEET_FLAG_USE
#define SHEET_HAS_CURSOR	SHEET_FLAG_HAS_CURSOR
#define SHEET_APP_WIN		SHEET_FLAG_APP_WIN
#define SHEET_RESIZABLE		SHEET_FLAG_RESIZABLE

struct SHTCTL *shtctl_init(struct MEMMAN *memman, unsigned char *vram, int xsize, int ysize)
{
	struct SHTCTL *ctl;
	int i;
	ctl = (struct SHTCTL *) memman_alloc_4k(memman, sizeof (struct SHTCTL));
	if (ctl == 0) {
		goto err;
	}
	ctl->map = (unsigned char *) memman_alloc_4k(memman, xsize * ysize);
	if (ctl->map == 0) {
		memman_free_4k(memman, (int) ctl, sizeof (struct SHTCTL));
		goto err;
	}
	ctl->vram = vram;
	ctl->xsize = xsize;
	ctl->ysize = ysize;
	ctl->top = -1; /* 시트는 한 장도 없다 */
	for (i = 0; i < MAX_SHEETS; i++) {
		ctl->sheets0[i].flags = 0; /* 미사용 마크 */
		ctl->sheets0[i].ctl = ctl; /* 소속을 기록 */
	}
err:
	return ctl;
}

struct SHEET *sheet_alloc(struct SHTCTL *ctl)
{
	struct SHEET *sht;
	int i;
	for (i = 0; i < MAX_SHEETS; i++) {
		if (ctl->sheets0[i].flags == 0) {
			sht = &ctl->sheets0[i];
			/* 기본값: 사용중 + 크기조정 가능 (요청대로 default true).
			 * 어플리케이션이 만든 윈도우는 api_openwin 쪽에서 RESIZABLE
			 * 비트를 다시 끈다 (앱 buffer 가 고정 크기이기 때문).      */
			sht->flags = SHEET_USE | SHEET_RESIZABLE;
			sht->height = -1; /* 비표시중 */
			sht->task = 0;	/* 자동으로 닫는 기능을 사용하지 않는다 */
			sht->scroll = 0;
			return sht;
		}
	}
	return 0;	/* 모든 시트가 사용중이었다 */
}

void sheet_setbuf(struct SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv)
{
	sht->buf = buf;
	sht->bxsize = xsize;
	sht->bysize = ysize;
	sht->col_inv = col_inv;
	return;
}

/* work6 Phase 3: 4-byte / 1-byte 분기 통합. 두 가지 path:
 *   (a) opaque sheet (col_inv == -1): row 마다 한 번 memset(map, sid, len). 가장
 *       흔한 case (윈도우/콘솔/메뉴 모두 opaque). vx0 정렬 무관, rep stosb 가
 *       byte loop 보다 5~10x.
 *   (b) transparent sheet (col_inv != -1, mouse cursor 등): row 마다 buf[] 의
 *       non-col_inv run 을 byte scan 으로 찾아 memset run. 마우스는 16x16 작은
 *       sheet 라 비용 미미하지만 알고리즘 일관. */
void sheet_refreshmap(struct SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0)
{
	int h, bx, by, bx0, by0, bx1, by1;
	unsigned char *buf, sid, *map = ctl->map;
	struct SHEET *sht;
	bench_enter(BENCH_REFRESHMAP);
	if (vx0 < 0) { vx0 = 0; }
	if (vy0 < 0) { vy0 = 0; }
	if (vx1 > ctl->xsize) { vx1 = ctl->xsize; }
	if (vy1 > ctl->ysize) { vy1 = ctl->ysize; }
	for (h = h0; h <= ctl->top; h++) {
		sht = ctl->sheets[h];
		sid = sht - ctl->sheets0; /* 번지를 빼서 그것을 레이어 번호로 이용 */
		buf = sht->buf;
		bx0 = vx0 - sht->vx0;
		by0 = vy0 - sht->vy0;
		bx1 = vx1 - sht->vx0;
		by1 = vy1 - sht->vy0;
		if (bx0 < 0) { bx0 = 0; }
		if (by0 < 0) { by0 = 0; }
		if (bx1 > sht->bxsize) { bx1 = sht->bxsize; }
		if (by1 > sht->bysize) { by1 = sht->bysize; }
		if (bx0 >= bx1 || by0 >= by1) continue;
		if (sht->col_inv == -1) {
			/* opaque: 한 row 통째로 memset (rep stosb). */
			int row_w = bx1 - bx0;
			for (by = by0; by < by1; by++) {
				memset(map + (sht->vy0 + by) * ctl->xsize + sht->vx0 + bx0,
						sid, (size_t) row_w);
			}
		} else {
			/* transparent: buf[byte] != col_inv 인 run 만 map 에 sid 기록. */
			int col_inv = sht->col_inv;
			for (by = by0; by < by1; by++) {
				int row_off_map = (sht->vy0 + by) * ctl->xsize + sht->vx0;
				int row_off_buf = by * sht->bxsize;
				int run_start = -1;
				for (bx = bx0; bx < bx1; bx++) {
					if (buf[row_off_buf + bx] != col_inv) {
						if (run_start < 0) run_start = bx;
					} else if (run_start >= 0) {
						memset(map + row_off_map + run_start, sid,
								(size_t)(bx - run_start));
						run_start = -1;
					}
				}
				if (run_start >= 0) {
					memset(map + row_off_map + run_start, sid,
							(size_t)(bx - run_start));
				}
			}
		}
	}
	bench_leave(BENCH_REFRESHMAP);
	return;
}

/* work6 Phase 2: 4-byte path 와 1-byte path 통합. row 마다 map[] 의 sid run
 * 을 byte scan 으로 찾아서 memcpy. unaligned vx0 도 자연스럽게 처리. memcpy
 * 는 modern_libc 의 rep movsb 라 큰 run 에서 byte-store 대비 5~10x. */
void sheet_refreshsub(struct SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0, int h1)
{
	int h, bx, by, bx0, by0, bx1, by1;
	unsigned char *buf, *vram = ctl->vram, *map = ctl->map, sid;
	struct SHEET *sht;
	bench_enter(BENCH_REFRESHSUB);
	/* refresh 범위가 화면외에 있으면 보정 */
	if (vx0 < 0) { vx0 = 0; }
	if (vy0 < 0) { vy0 = 0; }
	if (vx1 > ctl->xsize) { vx1 = ctl->xsize; }
	if (vy1 > ctl->ysize) { vy1 = ctl->ysize; }
	for (h = h0; h <= h1; h++) {
		sht = ctl->sheets[h];
		buf = sht->buf;
		sid = sht - ctl->sheets0;
		/* vx0~vy1를 사용해, bx0~by1를 역산한다 */
		bx0 = vx0 - sht->vx0;
		by0 = vy0 - sht->vy0;
		bx1 = vx1 - sht->vx0;
		by1 = vy1 - sht->vy0;
		if (bx0 < 0) { bx0 = 0; }
		if (by0 < 0) { by0 = 0; }
		if (bx1 > sht->bxsize) { bx1 = sht->bxsize; }
		if (by1 > sht->bysize) { by1 = sht->bysize; }
		for (by = by0; by < by1; by++) {
			int row_off = (sht->vy0 + by) * ctl->xsize + sht->vx0;
			int buf_off = by * sht->bxsize;
			int run_start = -1;
			for (bx = bx0; bx < bx1; bx++) {
				if (map[row_off + bx] == sid) {
					if (run_start < 0) run_start = bx;
				} else if (run_start >= 0) {
					int len = bx - run_start;
					memcpy(vram + row_off + run_start,
							buf + buf_off + run_start, (size_t) len);
					run_start = -1;
				}
			}
			if (run_start >= 0) {
				int len = bx - run_start;
				memcpy(vram + row_off + run_start,
						buf + buf_off + run_start, (size_t) len);
			}
		}
	}
	bench_leave(BENCH_REFRESHSUB);
	return;
}

void sheet_updown(struct SHEET *sht, int height)
{
	struct SHTCTL *ctl = sht->ctl;
	int h, old = sht->height; /* 설정전의 높이를 기억한다 */

	/* 지정이 너무 낮거나 너무 높으면 수정한다 */
	if (height > ctl->top + 1) {
		height = ctl->top + 1;
	}
	if (height < -1) {
		height = -1;
	}
	sht->height = height; /* 높이를 설정 */

	/* 이하는 주로 sheets[]를 늘어놓고 대체 */
	if (old > height) {	/* 이전보다 낮아진다 */
		if (height >= 0) {
			/* 사이의 것을 끌어올린다 */
			for (h = old; h > height; h--) {
				ctl->sheets[h] = ctl->sheets[h - 1];
				ctl->sheets[h]->height = h;
			}
			ctl->sheets[height] = sht;
			sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height + 1);
			sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height + 1, old);
		} else {	/* 비표시화 */
			if (ctl->top > old) {
				/* 위로 되어 있는 것을 내린다 */
				for (h = old; h < ctl->top; h++) {
					ctl->sheets[h] = ctl->sheets[h + 1];
					ctl->sheets[h]->height = h;
				}
			}
			ctl->top--; /* 표시중의 레이어가 1개 줄어들므로, 맨 위의 높이가 줄어든다 */
			sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, 0);
			sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, 0, old - 1);
		}
	} else if (old < height) {	/* 이전보다 높아진다 */
		if (old >= 0) {
			/* 사이의 것을 눌러 내린다 */
			for (h = old; h < height; h++) {
				ctl->sheets[h] = ctl->sheets[h + 1];
				ctl->sheets[h]->height = h;
			}
			ctl->sheets[height] = sht;
		} else {	/* 비표시 상태에서 표시 상태로 */
			/* 위가 되는 것을 들어 올린다 */
			for (h = ctl->top; h >= height; h--) {
				ctl->sheets[h + 1] = ctl->sheets[h];
				ctl->sheets[h + 1]->height = h + 1;
			}
			ctl->sheets[height] = sht;
			ctl->top++; /* 표시중의 레이어가 1개 증가하므로, 맨 위의 높이가 증가한다 */
		}
		sheet_refreshmap(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height);
		sheet_refreshsub(ctl, sht->vx0, sht->vy0, sht->vx0 + sht->bxsize, sht->vy0 + sht->bysize, height, height);
	}
	return;
}

void sheet_refresh(struct SHEET *sht, int bx0, int by0, int bx1, int by1)
{
	if (sht->height >= 0) { /* 만약 표시중이라면, 새로운 레이어의 정보에 따라 화면을 다시 그린다 */
		sheet_refreshsub(sht->ctl, sht->vx0 + bx0, sht->vy0 + by0, sht->vx0 + bx1, sht->vy0 + by1, sht->height, sht->height);
	}
	return;
}

void sheet_slide(struct SHEET *sht, int vx0, int vy0)
{
	struct SHTCTL *ctl = sht->ctl;
	int old_vx0 = sht->vx0, old_vy0 = sht->vy0;
	bench_enter(BENCH_SHEET_SLIDE);
	sht->vx0 = vx0;
	sht->vy0 = vy0;
	if (sht->height >= 0) { /* 만약 표시중이라면, 새로운 레이어의 정보에 따라 화면을 다시 그린다 */
		sheet_refreshmap(ctl, old_vx0, old_vy0, old_vx0 + sht->bxsize, old_vy0 + sht->bysize, 0);
		sheet_refreshmap(ctl, vx0, vy0, vx0 + sht->bxsize, vy0 + sht->bysize, sht->height);
		sheet_refreshsub(ctl, old_vx0, old_vy0, old_vx0 + sht->bxsize, old_vy0 + sht->bysize, 0, sht->height - 1);
		sheet_refreshsub(ctl, vx0, vy0, vx0 + sht->bxsize, vy0 + sht->bysize, sht->height, sht->height);
	}
	bench_leave(BENCH_SHEET_SLIDE);
	return;
}

void sheet_free(struct SHEET *sht)
{
	if (sht->height >= 0) {
		sheet_updown(sht, -1); /* 표시중이라면 우선 비표시로 한다 */
	}
	sht->flags = 0; /* 미사용 마크 */
	return;
}

/* 시트의 buffer 와 크기를 교체. 위치(vx0,vy0) 는 유지.
 * 호출자가 new_buf 안에 새 그림(프레임/배경)을 미리 그려서 넘겨야 한다.
 * 이전 buf 의 free 책임은 호출자에게 있다 (sheet_resize 는 모름).        */
void sheet_resize(struct SHEET *sht, unsigned char *new_buf,
		int new_xsize, int new_ysize)
{
	struct SHTCTL *ctl = sht->ctl;
	int old_xsize = sht->bxsize, old_ysize = sht->bysize;
	int old_vx0  = sht->vx0,    old_vy0  = sht->vy0;
	sht->buf    = new_buf;
	sht->bxsize = new_xsize;
	sht->bysize = new_ysize;
	if (sht->height >= 0) {
		/* 옛 영역과 새 영역 모두 다시 그려야 한다. */
		int union_x1 = old_vx0 + (old_xsize > new_xsize ? old_xsize : new_xsize);
		int union_y1 = old_vy0 + (old_ysize > new_ysize ? old_ysize : new_ysize);
		sheet_refreshmap(ctl, old_vx0, old_vy0, union_x1, union_y1, 0);
		sheet_refreshsub(ctl, old_vx0, old_vy0, union_x1, union_y1, 0, ctl->top);
	}
	return;
}
