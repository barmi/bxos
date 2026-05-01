/* 그래픽 처리 관계 */

#include "bootpack.h"
#include "../../tools/modern/euckr_map.h"

int g_background_color = COL8_008484;
int g_start_menu_open = 0;
int g_clock_minutes = 0;

void init_palette(void)
{
	static unsigned char table_rgb[16 * 3] = {
		0x00, 0x00, 0x00,	/*  0:흑 */
		0xff, 0x00, 0x00,	/*  1:밝은 빨강 */
		0x00, 0xff, 0x00,	/*  2:밝은 초록 */
		0xff, 0xff, 0x00,	/*  3:밝은 황색 */
		0x00, 0x00, 0xff,	/*  4:밝은 파랑 */
		0xff, 0x00, 0xff,	/*  5:밝은 보라색 */
		0x00, 0xff, 0xff,	/*  6:밝은 물색 */
		0xff, 0xff, 0xff,	/*  7:흰색 */
		0xc6, 0xc6, 0xc6,	/*  8:밝은 회색 */
		0x84, 0x00, 0x00,	/*  9:어두운 빨강 */
		0x00, 0x84, 0x00,	/* 10:어두운 초록 */
		0x84, 0x84, 0x00,	/* 11:어두운 황색 */
		0x00, 0x00, 0x84,	/* 12:어두운 파랑 */
		0x84, 0x00, 0x84,	/* 13:어두운 보라색 */
		0x00, 0x84, 0x84,	/* 14:어두운 물색 */
		0x84, 0x84, 0x84	/* 15:어두운 회색 */
	};
	unsigned char table2[216 * 3];
	int r, g, b;
	set_palette(0, 15, table_rgb);
	for (b = 0; b < 6; b++) {
		for (g = 0; g < 6; g++) {
			for (r = 0; r < 6; r++) {
				table2[(r + g * 6 + b * 36) * 3 + 0] = r * 51;
				table2[(r + g * 6 + b * 36) * 3 + 1] = g * 51;
				table2[(r + g * 6 + b * 36) * 3 + 2] = b * 51;
			}
		}
	}
	set_palette(16, 231, table2);
	return;
}

void set_palette(int start, int end, unsigned char *rgb)
{
	int i, eflags;
	eflags = io_load_eflags();	/* 인터럽트 허가 플래그의 값을 기록한다 */
	io_cli(); 			/* 허가 플래그를 0으로 해 인터럽트 금지로 한다 */
	io_out8(0x03c8, start);
	for (i = start; i <= end; i++) {
		io_out8(0x03c9, rgb[0] / 4);
		io_out8(0x03c9, rgb[1] / 4);
		io_out8(0x03c9, rgb[2] / 4);
		rgb += 3;
	}
	io_store_eflags(eflags);	/* 인터럽트 허가 플래그를 원래대로 되돌린다 */
	return;
}

void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1)
{
	int x, y;
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++)
			vram[y * xsize + x] = c;
	}
	return;
}

void init_screen8(char *vram, int x, int y)
{
	boxfill8(vram, x, g_background_color,  0,     0,      x -  1, y - 29);
	taskbar_redraw(vram, x, y, 0, 0);
	return;
}

static void taskbar_putascii(char *vram, int xsize, int x, int y,
		unsigned char c, unsigned char *s)
{
	extern char hankaku[4096];
	for (; *s != 0x00; s++) {
		putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
		x += 8;
	}
	return;
}

void taskbar_redraw(char *vram, int x, int y, int start_hover, int start_pressed)
{
	char clock[6];
	int sx0 = TASKBAR_START_X0, sx1 = TASKBAR_START_X1;
	int sy0 = y - TASKBAR_START_Y_PAD_TOP, sy1 = y - TASKBAR_START_Y_PAD_BOT;
	int tx0 = x - TASKBAR_TRAY_R_PAD - TASKBAR_TRAY_W;
	int tx1 = x - TASKBAR_TRAY_R_PAD - 1;
	int ty0 = sy0, ty1 = sy1;
	int start_down = start_pressed || g_start_menu_open;
	int label_x = 12 + (start_down ? 1 : 0);
	int label_y = y - 21 + (start_down ? 1 : 0);
	int clock_min = g_clock_minutes % (24 * 60);
	int hh = clock_min / 60;
	int mm = clock_min % 60;
	clock[0] = '0' + hh / 10;
	clock[1] = '0' + hh % 10;
	clock[2] = ':';
	clock[3] = '0' + mm / 10;
	clock[4] = '0' + mm % 10;
	clock[5] = 0;

	boxfill8(vram, x, COL8_C6C6C6,  0,     y - 28, x -  1, y - 28);
	boxfill8(vram, x, COL8_FFFFFF,  0,     y - 27, x -  1, y - 27);
	boxfill8(vram, x, COL8_C6C6C6,  0,     y - 26, x -  1, y -  1);

	boxfill8(vram, x, COL8_C6C6C6, sx0,     sy0,     sx1,     sy1);
	if (start_down) {
		boxfill8(vram, x, COL8_000000, sx0,     sy0,     sx1,     sy0);
		boxfill8(vram, x, COL8_000000, sx0,     sy0,     sx0,     sy1);
		boxfill8(vram, x, COL8_848484, sx0 + 1, sy0 + 1, sx1 - 1, sy0 + 1);
		boxfill8(vram, x, COL8_848484, sx0 + 1, sy0 + 1, sx0 + 1, sy1 - 1);
		boxfill8(vram, x, COL8_FFFFFF, sx0 + 1, sy1 - 1, sx1 - 1, sy1 - 1);
		boxfill8(vram, x, COL8_FFFFFF, sx1 - 1, sy0 + 1, sx1 - 1, sy1 - 1);
	} else {
		boxfill8(vram, x, COL8_FFFFFF, sx0 + 1, sy0,     sx1 - 1, sy0);
		boxfill8(vram, x, COL8_FFFFFF, sx0,     sy0,     sx0,     sy1);
		boxfill8(vram, x, COL8_848484, sx0 + 1, sy1,     sx1 - 1, sy1);
		boxfill8(vram, x, COL8_848484, sx1 - 1, sy0 + 1, sx1 - 1, sy1 - 1);
		boxfill8(vram, x, COL8_000000, sx0,     sy1 + 1, sx1 - 1, sy1 + 1);
		boxfill8(vram, x, COL8_000000, sx1,     sy0,     sx1,     sy1 + 1);
	}
	if (start_hover && !start_down) {
		boxfill8(vram, x, COL8_FFFFFF, sx0 + 4, sy0 + 3, sx1 - 5, sy0 + 3);
	}
	taskbar_putascii(vram, x, label_x, label_y, COL8_000000, (unsigned char *) "Start");

	boxfill8(vram, x, COL8_C6C6C6, tx0,     ty0,     tx1,     ty1);
	boxfill8(vram, x, COL8_848484, tx0,     ty0,     tx1,     ty0);
	boxfill8(vram, x, COL8_848484, tx0,     ty0,     tx0,     ty1);
	boxfill8(vram, x, COL8_FFFFFF, tx0,     ty1,     tx1,     ty1);
	boxfill8(vram, x, COL8_FFFFFF, tx1,     ty0,     tx1,     ty1);
	taskbar_putascii(vram, x, tx0 + 4, y - 20, COL8_000000, (unsigned char *) clock);
	return;
}

void putfont8(char *vram, int xsize, int x, int y, char c, char *font)
{
	int i;
	char *p, d /* data */;
	for (i = 0; i < 16; i++) {
		p = vram + (y + i) * xsize + x;
		d = font[i];
		if ((d & 0x80) != 0) { p[0] = c; }
		if ((d & 0x40) != 0) { p[1] = c; }
		if ((d & 0x20) != 0) { p[2] = c; }
		if ((d & 0x10) != 0) { p[3] = c; }
		if ((d & 0x08) != 0) { p[4] = c; }
		if ((d & 0x04) != 0) { p[5] = c; }
		if ((d & 0x02) != 0) { p[6] = c; }
		if ((d & 0x01) != 0) { p[7] = c; }
	}
	return;
}

void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s)
{
	extern char hankaku[4096];
	struct TASK *task = task_now();
	char *nihongo = (char *) *((int *) 0x0fe8), *font;
	int k, t;

	if (task->langmode == 0) {
		for (; *s != 0x00; s++) {
			putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
			x += 8;
		}
	}
	if (task->langmode == 1) {
		for (; *s != 0x00; s++) {
			if (task->langbyte1 == 0) {
				if ((0x81 <= *s && *s <= 0x9f) || (0xe0 <= *s && *s <= 0xfc)) {
					task->langbyte1 = *s;
				} else {
					putfont8(vram, xsize, x, y, c, nihongo + *s * 16);
				}
			} else {
				if (0x81 <= task->langbyte1 && task->langbyte1 <= 0x9f) {
					k = (task->langbyte1 - 0x81) * 2;
				} else {
					k = (task->langbyte1 - 0xe0) * 2 + 62;
				}
				if (0x40 <= *s && *s <= 0x7e) {
					t = *s - 0x40;
				} else if (0x80 <= *s && *s <= 0x9e) {
					t = *s - 0x80 + 63;
				} else {
					t = *s - 0x9f;
					k++;
				}
				task->langbyte1 = 0;
				font = nihongo + 256 * 16 + (k * 94 + t) * 32;
				putfont8(vram, xsize, x - 8, y, c, font     );	/* 왼쪽 반 */
				putfont8(vram, xsize, x    , y, c, font + 16);	/* 오른쪽 반 */
			}
			x += 8;
		}
	}
	if (task->langmode == 2) {
		for (; *s != 0x00; s++) {
			if (task->langbyte1 == 0) {
				if (0x81 <= *s && *s <= 0xfe) {
					task->langbyte1 = *s;
				} else {
					putfont8(vram, xsize, x, y, c, nihongo + *s * 16);
				}
			} else {
				k = task->langbyte1 - 0xa1;
				t = *s - 0xa1;
				task->langbyte1 = 0;
				font = nihongo + 256 * 16 + (k * 94 + t) * 32;
				putfont8(vram, xsize, x - 8, y, c, font     );	/* 왼쪽 반 */
				putfont8(vram, xsize, x    , y, c, font + 16);	/* 오른쪽 반 */
			}
			x += 8;
		}
	}
	if (task->langmode == 3) {
		char *hangul = (char *) *((int *) 0x0fe0);
		for (; *s != 0x00; s++) {
			if (hangul == 0) {
				putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				x += 8;
				continue;
			}
			if (task->langbyte1 == 0) {
				if (0xa1 <= *s && *s <= 0xfe) {
					task->langbyte1 = *s;
				} else {
					putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				}
			} else {
				int lead = task->langbyte1;
				int trail = *s;
				int idx;
				task->langbyte1 = 0;
				if (0xa1 <= lead && lead <= 0xfe && 0xa1 <= trail && trail <= 0xfe) {
					idx = g_euckr_to_uhs[(lead - 0xa1) * 94 + (trail - 0xa1)];
					if (idx != 0xffff) {
						font = hangul + 4096 + (idx - 0xac00) * 32;
						putfont8(vram, xsize, x - 8, y, c, font     );	/* 왼쪽 반 */
						putfont8(vram, xsize, x    , y, c, font + 16);	/* 오른쪽 반 */
					} else {
						putfont8(vram, xsize, x - 8, y, c, hankaku + lead * 16);
						putfont8(vram, xsize, x    , y, c, hankaku + trail * 16);
					}
				} else {
					putfont8(vram, xsize, x - 8, y, c, hankaku + lead * 16);
					putfont8(vram, xsize, x    , y, c, hankaku + trail * 16);
				}
			}
			x += 8;
		}
	}
	if (task->langmode == 4) {
		char *hangul = (char *) *((int *) 0x0fe0);
		for (; *s != 0x00; s++) {
			if (hangul == 0) {
				putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				x += 8;
				continue;
			}
			if (task->langbyte1 == 0) {
				if (*s < 0x80) {
					putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				} else if (0xea <= (unsigned char)*s && (unsigned char)*s <= 0xed) {
					task->langbyte1 = *s;
				} else {
					putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				}
			} else if (task->langbyte2 == 0) {
				if (0x80 <= (unsigned char)*s && (unsigned char)*s <= 0xbf) {
					task->langbyte2 = *s;
					x -= 8;
				} else {
					task->langbyte1 = 0;
					putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				}
			} else {
				if (0x80 <= (unsigned char)*s && (unsigned char)*s <= 0xbf) {
					int cp = ((task->langbyte1 & 0x0f) << 12) | ((task->langbyte2 & 0x3f) << 6) | (*s & 0x3f);
					if (0xac00 <= cp && cp <= 0xd7a3) {
						char *font = hangul + 4096 + (cp - 0xac00) * 32;
						putfont8(vram, xsize, x - 8, y, c, font     );	/* 왼쪽 반 */
						putfont8(vram, xsize, x    , y, c, font + 16);	/* 오른쪽 반 */
					} else {
						putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
					}
				} else {
					putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
				}
				task->langbyte1 = 0;
				task->langbyte2 = 0;
			}
			x += 8;
		}
	}
	return;
}

void init_mouse_cursor8(char *mouse, char bc)
/* 마우스 커서를 준비(16 x16) */
{
	static char cursor[MAX_MOUSE_CURSOR][16][16] = {
		{
		"*...............",
		"**..............",
		"*O*.............",
		"*OO*............",
		"*OOO*...........",
		"*OOOO*..........",
		"*OOOOO*.........",
		"*OOOOOO*........",
		"*OOOOOOO*.......",
		"*OOOOOOOO*......",
		"*OOOOOOOOO*.....",
		"*OOO*OO*****....",
		"*OO*.*OO*.......",
		"*O*..*OO*.......",
		"**....*OO*......",
		".......**......."
		},{
		"********........",
		"*OOOOOO*........",
		"*OOOOO*.........",
		"*OOOO*..........",
		"*OOOO*..........",
		"*OO**O*.........",
		"*O*..*O*........",
		"**....*O*.......",
		".......*O*....**",
		"........*O*..*O*",
		".........*O**OO*",
		"..........*OOOO*",
		"..........*OOOO*",
		".........*OOOOO*",
		"........*OOOOOO*",
		"........********"
		},{
		"................",
		"................",
		"................",
		"................",
		"...*........*...",
		"..*O*......*O*..",
		".*OO********OO*.",
		"*OOOOOOOOOOOOOO*",
		"*OOOOOOOOOOOOOO*",
		".*OO********OO*.",
		"..*O*......*O*..",
		"...*........*...",
		"................",
		"................",
		"................",
		"................"
		},{
		".......**.......",
		"......*OO*......",
		".....*OOOO*.....",
		"....*OOOOOO*....",
		".......**.......",
		".......**.......",
		".......**.......",
		".......**.......",
		".......**.......",
		".......**.......",
		".......**.......",
		".......**.......",
		"....*OOOOOO*....",
		".....*OOOO*.....",
		"......*OO*......",
		".......**......."
		}
/*
		"**************..",
		"*OOOOOOOOOOO*...",
		"*OOOOOOOOOO*....",
		"*OOOOOOOOO*.....",
		"*OOOOOOOO*......",
		"*OOOOOOO*.......",
		"*OOOOOOO*.......",
		"*OOOOOOOO*......",
		"*OOOO**OOO*.....",
		"*OOO*..*OOO*....",
		"*OO*....*OOO*...",
		"*O*......*OOO*..",
		"**........*OOO*.",
		"*..........*OOO*",
		"............*OO*",
		".............***"
*/
	};
	int x, y, c;

	for (c = 0; c < MAX_MOUSE_CURSOR; c++)
	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x++) {
			if (cursor[c][y][x] == '*') {
				mouse[c * SIZE_MOUSE_CURSOR + y * 16 + x] = COL8_000000;
			}
			if (cursor[c][y][x] == 'O') {
				mouse[c * SIZE_MOUSE_CURSOR + y * 16 + x] = COL8_FFFFFF;
			}
			if (cursor[c][y][x] == '.') {
				mouse[c * SIZE_MOUSE_CURSOR + y * 16 + x] = bc;
			}
		}
	}
	return;
}

void putblock8_8(char *vram, int vxsize, int pxsize,
	int pysize, int px0, int py0, char *buf, int bxsize)
{
	int x, y;
	for (y = 0; y < pysize; y++) {
		for (x = 0; x < pxsize; x++) {
			vram[(py0 + y) * vxsize + (px0 + x)] = buf[y * bxsize + x];
		}
	}
	return;
}
