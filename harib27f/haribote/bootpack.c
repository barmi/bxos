/* bootpack의 메인 */

#include "bootpack.h"
#include <stdio.h>
#include <string.h>

#define KEYCMD_LED		0xed

struct TASK *fdc, *inout, *taskmgr;

void keywin_off(struct SHEET *key_win);
void close_console(struct SHEET *sht);
void close_constask(struct TASK *task);
void close_taskmgr(void);
void init_menu(struct MNLV *mnlv, struct MENU **menu);
void scrollwin_window_resize(struct SHEET *sht, int new_w, int new_h, char *title);

unsigned int g_memtotal = 0;	/* HariMain memtest 결과 — work4 api_exec 등에서 참조 */
struct SHEET *g_sht_mouse = 0;
unsigned char *g_mouse_buf = 0;
int g_mouse_cursor = BX_CURSOR_ARROW;

void mouse_set_cursor_shape(int shape)
{
	struct SHTCTL *ctl;
	if (shape < 0 || shape >= MAX_MOUSE_CURSOR) {
		shape = BX_CURSOR_ARROW;
	}
	if (g_sht_mouse == 0 || g_mouse_buf == 0 || shape == g_mouse_cursor) {
		return;
	}
	g_mouse_cursor = shape;
	g_sht_mouse->buf = g_mouse_buf + shape * SIZE_MOUSE_CURSOR;
	if (g_sht_mouse->height >= 0) {
		ctl = g_sht_mouse->ctl;
		sheet_refreshmap(ctl, g_sht_mouse->vx0, g_sht_mouse->vy0,
				g_sht_mouse->vx0 + g_sht_mouse->bxsize,
				g_sht_mouse->vy0 + g_sht_mouse->bysize, 0);
		sheet_refreshsub(ctl, g_sht_mouse->vx0, g_sht_mouse->vy0,
				g_sht_mouse->vx0 + g_sht_mouse->bxsize,
				g_sht_mouse->vy0 + g_sht_mouse->bysize, 0, ctl->top);
	}
	return;
}

void HariMain(void)
{
	struct BOOTINFO *binfo = (struct BOOTINFO *) ADR_BOOTINFO;
	struct SHTCTL *shtctl;
	char s[40];
	struct FIFO32 fifo, keycmd;
	int fifobuf[128], keycmd_buf[32];
	int mx, my, i, new_mx = -1, new_my = 0, new_wx = 0x7fffffff, new_wy = 0;
	unsigned int memtotal;
	struct MOUSE_DEC mdec;
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	unsigned char *buf_back, buf_mouse[SIZE_MOUSE_CURSOR*MAX_MOUSE_CURSOR];
	struct SHEET *sht_back, *sht_mouse;
	struct TASK *task_a, *task;
	static char keytable0[0x80] = {
		0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x08,   0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0x0a,   0,   'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`',   0,   '\\', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
	};
	static char keytable1[0x80] = {
		0,   0,   '!', '@', '#', '$', '%', '&', '^', '*', '(', ')', '_', '+', 0x08,   0,
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0x0a,   0,   'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,   '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
	};
	int key_shift = 0, key_leds = (binfo->leds >> 4) & 7, keycmd_wait = -1, key_e0 = 0;
	int j, x, y, mmx = -1, mmy = -1, mmx2 = 0;
	/* ── 윈도우 리사이즈 모드 상태 ─────────────────────────────────
	 * rsht != 0 이면 리사이즈 중. (rmx, rmy) 시작 마우스, (rw0, rh0) 시작
	 * 사이즈, (redge & RZ_*) 가 잡은 위치 (오른쪽/아래/오른아래) 비트.    */
	struct SHEET *rsht = 0;
	int rmx = 0, rmy = 0, rw0 = 0, rh0 = 0, redge = 0, new_rw = 0, new_rh = 0;
	int rpost_w = 0, rpost_h = 0;
#define RZ_RIGHT  1
#define RZ_BOTTOM 2
#define RZ_HANDLE 14   /* 우하단 모서리 잡는 영역(px) */
#define RZ_EDGE   4    /* 우/하단 엣지 폭(px) */
#define RZ_MIN_W  64
#define RZ_MIN_H  48
	/* work4 Phase 2: app client mouse event 라우팅 — 이전 btn 상태 추적. */
	int old_btn = 0;
	struct SHEET *sht = 0, *key_win, *sht2;
	int *fat;
	unsigned char *nihongo, *hangul;
	struct FILEINFO *finfo;
	extern char hankaku[4096];

	init_gdtidt();
	init_pic();
	io_sti(); /* IDT/PIC의 초기화가 끝났으므로 CPU의 인터럽트 금지를 해제 */
	fifo32_init(&fifo, 128, fifobuf, 0);
	*((int *) 0x0fec) = (int) &fifo;
	init_pit();
	init_keyboard(&fifo, 256);
	enable_mouse(&fifo, 512, &mdec);
	io_out8(PIC0_IMR, 0xf8); /* PIT와 PIC1와 키보드를 허가(11111000) */
	io_out8(PIC1_IMR, 0xef); /* 마우스를 허가(11101111) */
	fifo32_init(&keycmd, 32, keycmd_buf, 0);

	/* ATA 디스크 검사 (work1 Phase 2). 결과는 ata_drive_info[] 에 캐시되며
	 * 콘솔 명령 `disk` 로 확인 가능. */
	ata_init();

	memtotal = memtest(0x00400000, 0xbfffffff);
	g_memtotal = memtotal;
	memman_init(memman);
	memman_free(memman, 0x00001000, 0x0009e000); /* 0x00001000 - 0x0009efff */
	memman_free(memman, 0x00400000, memtotal - 0x00400000);

	/* 데이터 드라이브 마운트 (work1 Phase 3). master(0) 의 BPB 를 읽고
	 * FAT/루트디렉터리를 메모리에 캐시한다. 이후 콘솔의 `dir` 과 앱 실행은
	 * 이 마운트(g_data_mount)를 통한다. memman 초기화 직후에 호출. */
	fs_mount_data(0);

	init_palette();
	shtctl = shtctl_init(memman, binfo->vram, binfo->scrnx, binfo->scrny);
	task_a = task_init(memman);
	fifo.task = task_a;
	task_run(task_a, 1, 2);
	*((int *) 0x0fe4) = (int) shtctl;
	task_a->langmode = 0;

	/* sht_back */
	sht_back  = sheet_alloc(shtctl);
	buf_back  = (unsigned char *) memman_alloc_4k(memman, binfo->scrnx * binfo->scrny);
	sheet_setbuf(sht_back, buf_back, binfo->scrnx, binfo->scrny, -1); /* 투명색없음 */
	init_screen8(buf_back, binfo->scrnx, binfo->scrny);

	/* nihongo.fnt / hangul.fnt의 read
	 * console_task() 는 시작 직후 폰트 포인터를 읽는다.
	 * 콘솔 태스크를 띄우기 전에 반드시 초기화해 둔다. */
	fat = (int *) memman_alloc_4k(memman, 4 * 2880);
	file_readfat(fat, (unsigned char *) (ADR_DISKIMG + 0x000200));

	finfo = file_search("nihongo.fnt", (struct FILEINFO *) (ADR_DISKIMG + 0x002600), 224);
	if (finfo != 0) {
		i = finfo->size;
		nihongo = file_loadfile2(finfo->clustno, &i, fat);
		for (i = 0; i < 16 * 256; i++) {
			nihongo[i] = hankaku[i];
		}
	} else {
		nihongo = (unsigned char *) memman_alloc_4k(memman, 16 * 256 + 32 * 94 * 47);
		for (i = 0; i < 16 * 256; i++) {
			nihongo[i] = hankaku[i]; /* 폰트가 없었기 때문에 반각 부분을 카피 */
		}
		for (i = 16 * 256; i < 16 * 256 + 32 * 94 * 47; i++) {
			nihongo[i] = 0xff; /* 폰트가 없었기 때문에 전각 부분을 0xff로 다 채운다 */
		}
	}
	*((int *) 0x0fe8) = (int) nihongo;

	/* hangul.fnt 는 ~353KB 라 부팅 FDD(IPL의 CYLS 한도) 에 끝까지 적재되지
	 * 않는다. 따라서 데이터 디스크(data.img)에 두고 ATA 경로로 읽는다. */
	finfo = fs_data_search("hangul.fnt");
	if (finfo != 0) {
		i = finfo->size;
		hangul = (unsigned char *) fs_data_loadfile(finfo->clustno, &i);
		if (hangul != 0) {
			for (i = 0; i < 16 * 256; i++) {
				hangul[i] = hankaku[i];
			}
		}
	} else {
		hangul = 0;
	}
	*((int *) 0x0fe0) = (int) hangul;
	memman_free_4k(memman, (int) fat, 4 * 2880);

	/* sht_cons */
	key_win = open_console(shtctl, memtotal);

	/* sht_mouse */
	sht_mouse = sheet_alloc(shtctl);
	sheet_setbuf(sht_mouse, buf_mouse, 16, 16, 99);
	init_mouse_cursor8(buf_mouse, 99);
	g_sht_mouse = sht_mouse;
	g_mouse_buf = buf_mouse;
	g_mouse_cursor = BX_CURSOR_ARROW;
	mx = (binfo->scrnx - 16) / 2; /* 화면 중앙이 되도록 좌표 계산 */
	my = (binfo->scrny - 28 - 16) / 2;

	sheet_slide(sht_back,  0,  0);
	sheet_slide(key_win,   32, 4);
	sheet_slide(sht_mouse, mx, my);
	sheet_updown(sht_back,  0);
	sheet_updown(key_win,   1);
	sheet_updown(sht_mouse, 2);
	keywin_on(key_win);

	/* 처음에 키보드 상태와 어긋나지 않게, 설정해 두기로 한다 */
	fifo32_put(&keycmd, KEYCMD_LED);
	fifo32_put(&keycmd, key_leds);

	// skshin 
	dbg_init(shtctl);
	sheet_slide(dbg_get()->sht, 300, 480);
	sheet_updown(dbg_get()->sht, shtctl->top);

	for (;;) {
		if (fifo32_status(&keycmd) > 0 && keycmd_wait < 0) {
			/* 키보드 컨트롤러에 보낼 데이터가 있으면, 보낸다 */
			keycmd_wait = fifo32_get(&keycmd);
			wait_KBC_sendready();
			io_out8(PORT_KEYDAT, keycmd_wait);
		}
		io_cli();
		if (fifo32_status(&fifo) == 0) {
			/* FIFO가 텅 비게 되었으므로, 보류하고 있는 그리기가 있으면 실행한다 */
			if (new_mx >= 0) {
				io_sti();
				sheet_slide(sht_mouse, new_mx, new_my);
				new_mx = -1;
			} else if (new_wx != 0x7fffffff) {
				io_sti();
				sheet_slide(sht, new_wx, new_wy);
				new_wx = 0x7fffffff;
			} else {
				task_sleep(task_a);
				io_sti();
			}
		} else {
			i = fifo32_get(&fifo);
			io_sti();
			if (key_win != 0 && key_win->flags == 0) {	/* 윈도우가 닫혀졌다 */
				if (shtctl->top == 1) {	/* 이제 마우스와 배경 밖에 없다 */
					key_win = 0;
				} else {
					key_win = shtctl->sheets[shtctl->top - 1];
					keywin_on(key_win);
				}
			}
			if (256 <= i && i <= 511) { /* 키보드 데이터 */
				/*
					keyboard code : http://ubuntuforums.org/archive/index.php/t-1059755.html
				*/
				if (i == 256 + 0xe0) {
					key_e0 = 1;
				} else if (key_e0 != 0) {
					key_e0 = 0;
					if (key_win != 0 && key_win->task != 0) {
						if (i == 256 + 0x48) {	/* ↑ */
							fifo32_put(&key_win->task->fifo, CONS_KEY_UP + 256);
						}
						if (i == 256 + 0x50) {	/* ↓ */
							fifo32_put(&key_win->task->fifo, CONS_KEY_DOWN + 256);
						}
						if (i == 256 + 0x4b) {	/* ← */
							fifo32_put(&key_win->task->fifo, CONS_KEY_LEFT + 256);
						}
						if (i == 256 + 0x4d) {	/* → */
							fifo32_put(&key_win->task->fifo, CONS_KEY_RIGHT + 256);
						}
					}
				} else {
					if (i < 0x80 + 256) { /* 키코드를 문자 코드로 변환 */
						if (key_shift == 0) {
							s[0] = keytable0[i - 256];
						} else {
							s[0] = keytable1[i - 256];
						}
					} else {
						s[0] = 0;
					}
					if ('A' <= s[0] && s[0] <= 'Z') {	/* 입력 문자가 알파벳 */
						if (((key_leds & 4) == 0 && key_shift == 0) ||
								((key_leds & 4) != 0 && key_shift != 0)) {
							s[0] += 0x20;	/* 대문자를 소문자로 변환 */
						}
					}
					if (s[0] != 0 && key_win != 0 && key_win->task != 0) { /* 통상 문자, 백 스페이스, Enter */
						fifo32_put(&key_win->task->fifo, s[0] + 256);
					}
					if (i == 256 + 0x0f && key_win != 0) {	/* Tab */
						keywin_off(key_win);
						j = key_win->height - 1;
						if (j == 0) {
							j = shtctl->top - 1;
						}
						key_win = shtctl->sheets[j];
						keywin_on(key_win);
					}
					if (i == 256 + 0x2a) {	/* 왼쪽 쉬프트 ON */
						key_shift |= 1;
					}
					if (i == 256 + 0x36) {	/* 오른쪽 쉬프트 ON */
						key_shift |= 2;
					}
					if (i == 256 + 0xaa) {	/* 왼쪽 쉬프트 OFF */
						key_shift &= ~1;
					}
					if (i == 256 + 0xb6) {	/* 오른쪽 쉬프트 OFF */
						key_shift &= ~2;
					}
					if (i == 256 + 0x3a) {	/* CapsLock */
						key_leds ^= 4;
						fifo32_put(&keycmd, KEYCMD_LED);
						fifo32_put(&keycmd, key_leds);
					}
					if (i == 256 + 0x45) {	/* NumLock */
						key_leds ^= 2;
						fifo32_put(&keycmd, KEYCMD_LED);
						fifo32_put(&keycmd, key_leds);
					}
					if (i == 256 + 0x46) {	/* ScrollLock */
						key_leds ^= 1;
						fifo32_put(&keycmd, KEYCMD_LED);
						fifo32_put(&keycmd, key_leds);
					}
					if (i == 256 + 0x3b && key_shift != 0 && key_win != 0 && key_win->task != 0) {	/* Shift+F1 */
						task = key_win->task;
						if (task != 0 && task->tss.ss0 != 0) {
							cons_putstr0(task->cons, "\nBreak(key) :\n");
							io_cli();	/* 강제 종료 처리중에 태스크가 바뀌면 곤란하기 때문에 */
							task->tss.eax = (int) &(task->tss.esp0);
							task->tss.eip = (int) asm_end_app;
							io_sti();
							task_run(task, -1, 0);	/* 종료 처리를 확실히 시키기 위해서, sleeve하고 있으면 깨운다 */
						}
					}
					if (i == 256 + 0x3c && key_shift != 0) {	/* Shift+F2 */
						/* 새롭게 만든 콘솔을 입력 선택 상태로 한다(그 편이 친절하겠지요? ) */
						if (key_win != 0) {
							keywin_off(key_win);
						}
						key_win = open_console(shtctl, memtotal);
						sheet_slide(key_win, 32, 4);
						sheet_updown(key_win, shtctl->top);
						keywin_on(key_win);
					}
					if (i == 256 + 0x57) {	/* F11 */
						sheet_updown(shtctl->sheets[1], shtctl->top - 1);
					}
					if (i == 256 + 0x58) {	/* F12 */
						/* create debug console */
					}
					if (i == 256 + 0xfa) {	/* 키보드가 데이터를 무사하게 받았다 */
						keycmd_wait = -1;
					}
					if (i == 256 + 0xfe) {	/* 키보드가 데이터를 무사하게 받을 수 없었다 */
						wait_KBC_sendready();
						io_out8(PORT_KEYDAT, keycmd_wait);
					}
				}
			} else if (512 <= i && i <= 767) { /* 마우스 데이터 */
				if (mouse_decode(&mdec, i - 512) != 0) {
					/* 마우스 커서의 이동 */
					mx += mdec.x;
					my += mdec.y;
					if (mx < 0) {
						mx = 0;
					}
					if (my < 0) {
						my = 0;
					}
					if (mx > binfo->scrnx - 1) {
						mx = binfo->scrnx - 1;
					}
					if (my > binfo->scrny - 1) {
						my = binfo->scrny - 1;
					}
					new_mx = mx;
					new_my = my;

					/* ── (a) 스크롤바가 이미 잡혀있다면 해당 텍스트 창으로 우회. */
					if (dbg_get()->sw.sb_grab) {
						scrollwin_handle_mouse(&dbg_get()->sw,
								mx - dbg_get()->sht->vx0, my - dbg_get()->sht->vy0, mdec.btn);
					} else if (key_win != 0 && key_win->scroll != 0 && key_win->scroll->sb_grab) {
						scrollwin_handle_mouse(key_win->scroll,
								mx - key_win->vx0, my - key_win->vy0, mdec.btn);
						/* 다른 모드 진입 안 함 — 다음 이벤트 대기 */
					} else if ((mdec.btn & 0x01) != 0) {
						/* 왼쪽 버튼을 누르고 있다 */
						if (rsht != 0) {
							/* (b) 윈도우 리사이즈 모드 진행중 */
							int dw = (redge & RZ_RIGHT)  ? (mx - rmx) : 0;
							int dh = (redge & RZ_BOTTOM) ? (my - rmy) : 0;
							new_rw = rw0 + dw;
							new_rh = rh0 + dh;
							if (new_rw < RZ_MIN_W) new_rw = RZ_MIN_W;
							if (new_rh < RZ_MIN_H) new_rh = RZ_MIN_H;
							if (rsht->vx0 + new_rw > binfo->scrnx) new_rw = binfo->scrnx - rsht->vx0;
							if (rsht->vy0 + new_rh > binfo->scrny) new_rh = binfo->scrny - rsht->vy0;
							if (new_rw != rsht->bxsize || new_rh != rsht->bysize) {
								if ((rsht->flags & SHEET_FLAG_APP_EVENTS) != 0 &&
										rsht->task != 0) {
									if (new_rw != rpost_w || new_rh != rpost_h) {
										bx_event_post(rsht->task, BX_EVENT_RESIZE,
												(int) rsht, 0, 0, 0, 0,
												new_rw, new_rh);
										rpost_w = new_rw;
										rpost_h = new_rh;
									}
								} else if ((rsht->flags & SHEET_FLAG_HAS_CURSOR) != 0) {
									console_resize(rsht, new_rw, new_rh);
								} else if ((rsht->flags & SHEET_FLAG_SCROLLWIN) != 0) {
									scrollwin_window_resize(rsht, new_rw, new_rh, "debug");
								}
							}
						} else if (mmx < 0) {
							/* 통상 모드의 경우 */
							/* 위 레이어부터 차례로 마우스가 가리키고 있는 레이어를 찾는다 */
							for (j = shtctl->top - 1; j > 0; j--) {
								sht = shtctl->sheets[j];
								x = mx - sht->vx0;
								y = my - sht->vy0;
								if (0 <= x && x < sht->bxsize && 0 <= y && y < sht->bysize) {
									if (sht->buf[y * sht->bxsize + x] != sht->col_inv) {
										sheet_updown(sht, shtctl->top - 1);
										if (sht != key_win) {
											keywin_off(key_win);
											key_win = sht;
											keywin_on(key_win);
										}
										if (sht->scroll != 0 &&
												scrollwin_handle_mouse(sht->scroll, x, y, mdec.btn) != 0) {
											break;
										}
										/* (c) 우하단 / 우엣지 / 하엣지 → 리사이즈 모드 */
										if ((sht->flags & SHEET_FLAG_RESIZABLE) != 0) {
											int hit_right  = (x >= sht->bxsize - RZ_EDGE);
											int hit_bottom = (y >= sht->bysize - RZ_EDGE);
											int hit_corner = (x >= sht->bxsize - RZ_HANDLE)
														  && (y >= sht->bysize - RZ_HANDLE);
											if (hit_corner || hit_right || hit_bottom) {
												rsht  = sht;
												rmx   = mx;
												rmy   = my;
												rw0   = sht->bxsize;
												rh0   = sht->bysize;
												new_rw = rw0;
												new_rh = rh0;
												rpost_w = rw0;
												rpost_h = rh0;
												redge = (hit_right || hit_corner ? RZ_RIGHT : 0)
												      | (hit_bottom || hit_corner ? RZ_BOTTOM : 0);
												break;	/* 리사이즈 모드 진입 */
											}
										}
										if (3 <= x && x < sht->bxsize - 3 && 3 <= y && y < 21) {
											mmx = mx;	/* 윈도우 이동 모드로 */
											mmy = my;
											mmx2 = sht->vx0;
											new_wy = sht->vy0;
										}
										if (sht->bxsize - 21 <= x && x < sht->bxsize - 5 && 5 <= y && y < 19) {
											/* 「×」버튼 클릭 */
											if ((sht->flags & 0x10) != 0) {		/* 어플리케이션이 만든 윈도우인가?  */
												task = sht->task;
												cons_putstr0(task->cons, "\nBreak(mouse) :\n");
												io_cli();	/* 강제 종료 처리중에 태스크가 바뀌면 곤란하기 때문에 */
												task->tss.eax = (int) &(task->tss.esp0);
												task->tss.eip = (int) asm_end_app;
												io_sti();
												task_run(task, -1, 0);
											} else if (sht->task != 0) {	/* 콘솔 */
												task = sht->task;
												sheet_updown(sht, -1); /* 우선 비표시로 해 둔다 */
												keywin_off(key_win);
												key_win = shtctl->sheets[shtctl->top - 1];
												keywin_on(key_win);
												io_cli();
												fifo32_put(&task->fifo, 4);
												io_sti();
											} else {	/* 태스크 없는 도구 창 */
												sheet_updown(sht, -1);
												if (sht == key_win) {
													key_win = shtctl->sheets[shtctl->top - 1];
													keywin_on(key_win);
												}
											}
										}
										break;
									}
								}
							}
						} else {
							/* 윈도우 이동 모드의 경우 */
							x = mx - mmx;	/* 마우스의 이동량을 계산 */
							y = my - mmy;
							new_wx = (mmx2 + x + 2) & ~3;
							new_wy = new_wy + y;
							mmy = my;	/* 이동 후의 좌표로 갱신 */
						}
					} else {
						/* 왼쪽 버튼을 누르지 않았다 */
						if (rsht != 0) {
							/* 리사이즈 드래그 종료. console/debug 처럼 라이브 리사이즈가
							 * 적용된 창은 그대로, app window (APP_EVENTS) 는 마지막
							 * 의도 크기 (new_rw, new_rh) 를 BX_EVENT_RESIZE 로 보낸다.  */
							if ((rsht->flags & SHEET_FLAG_APP_EVENTS) != 0 &&
									rsht->task != 0) {
								if (new_rw != rpost_w || new_rh != rpost_h) {
									bx_event_post(rsht->task, BX_EVENT_RESIZE,
											(int) rsht, 0, 0, 0, 0,
											new_rw, new_rh);
								}
							}
							rsht = 0;
							redge = 0;
						}
						mmx = -1;	/* 통상 모드로 */
						if (new_wx != 0x7fffffff) {
							sheet_slide(sht, new_wx, new_wy);	/* 한 번 확정시킨다 */
							new_wx = 0x7fffffff;
						}
					}

					/* ── work4 Phase 2: app client area mouse event 라우팅 ──
					 * scrollbar/title-drag/resize-drag/window-move 모드가 아닐 때만
					 * 발생. 좌표는 client-area 기준 (frame 3px + title 21px 빼고).      */
					if (rsht == 0 && mmx < 0 &&
							!dbg_get()->sw.sb_grab &&
							!(key_win != 0 && key_win->scroll != 0 &&
									key_win->scroll->sb_grab)) {
						struct SHEET *app_sht = 0;
						int app_x = 0, app_y = 0;
						int sj, sx, sy;
						for (sj = shtctl->top - 1; sj > 0; sj--) {
							struct SHEET *s = shtctl->sheets[sj];
							sx = mx - s->vx0;
							sy = my - s->vy0;
							if (0 <= sx && sx < s->bxsize &&
									0 <= sy && sy < s->bysize) {
								if (s->buf[sy * s->bxsize + sx] != s->col_inv) {
									if ((s->flags & SHEET_FLAG_APP_EVENTS) != 0 &&
											s->task != 0 &&
											3 <= sx && sx < s->bxsize - 3 &&
											21 <= sy && sy < s->bysize - 3) {
										int on_close = (sx >= s->bxsize - 21 &&
												sx <  s->bxsize -  5 &&
												sy >= 5 && sy < 19);
										int on_resize = ((s->flags & SHEET_FLAG_RESIZABLE) != 0) &&
												((sx >= s->bxsize - RZ_EDGE) ||
												 (sy >= s->bysize - RZ_EDGE));
										if (!on_close && !on_resize) {
											app_sht = s;
											app_x = sx - 3;
											app_y = sy - 21;
										}
									}
									break;
								}
							}
						}
						if (app_sht != 0) {
							int btn_dn = (~old_btn & mdec.btn) & 0x07;
							int btn_up = (old_btn & ~mdec.btn) & 0x07;
							if (btn_dn != 0) {
								bx_event_post(app_sht->task,
										BX_EVENT_MOUSE_DOWN,
										(int) app_sht, app_x, app_y,
										mdec.btn, 0, 0, 0);
							}
							if (btn_up != 0) {
								bx_event_post(app_sht->task,
										BX_EVENT_MOUSE_UP,
										(int) app_sht, app_x, app_y,
										mdec.btn, 0, 0, 0);
							}
							if (btn_dn == 0 && btn_up == 0 &&
									(mdec.x != 0 || mdec.y != 0)) {
								bx_event_post(app_sht->task,
										BX_EVENT_MOUSE_MOVE,
										(int) app_sht, app_x, app_y,
										mdec.btn, 0, 0, 0);
							}
						}
					}
					/* Resize 가능한 창의 우/하단 edge 에 올라가면 커서 모양을 바꾼다.
					 * 실제 resize hit-test 와 같은 조건을 써서 "잡히는 영역"이 보이게 한다. */
					{
						int next_cursor = BX_CURSOR_ARROW;
						if (rsht != 0) {
							if ((redge & (RZ_RIGHT | RZ_BOTTOM)) == (RZ_RIGHT | RZ_BOTTOM)) {
								next_cursor = BX_CURSOR_RESIZE_NWSE;
							} else if ((redge & RZ_RIGHT) != 0) {
								next_cursor = BX_CURSOR_RESIZE_WE;
							} else if ((redge & RZ_BOTTOM) != 0) {
								next_cursor = BX_CURSOR_RESIZE_NS;
							}
						} else if (mmx < 0 &&
								!dbg_get()->sw.sb_grab &&
								!(key_win != 0 && key_win->scroll != 0 &&
										key_win->scroll->sb_grab)) {
							int sj, sx, sy;
							for (sj = shtctl->top - 1; sj > 0; sj--) {
								struct SHEET *s = shtctl->sheets[sj];
								if (s == sht_mouse) {
									continue;
								}
								sx = mx - s->vx0;
								sy = my - s->vy0;
								if (0 <= sx && sx < s->bxsize &&
										0 <= sy && sy < s->bysize) {
									if (s->buf[sy * s->bxsize + sx] != s->col_inv) {
										if ((s->flags & SHEET_FLAG_RESIZABLE) != 0) {
											int hit_right  = (sx >= s->bxsize - RZ_EDGE);
											int hit_bottom = (sy >= s->bysize - RZ_EDGE);
											int hit_corner = (sx >= s->bxsize - RZ_HANDLE)
														  && (sy >= s->bysize - RZ_HANDLE);
											if (hit_corner) {
												next_cursor = BX_CURSOR_RESIZE_NWSE;
											} else if (hit_right) {
												next_cursor = BX_CURSOR_RESIZE_WE;
											} else if (hit_bottom) {
												next_cursor = BX_CURSOR_RESIZE_NS;
											}
										}
										break;
									}
								}
							}
						}
						mouse_set_cursor_shape(next_cursor);
					}
					old_btn = mdec.btn;
				}
			} else if (768 <= i && i <= 1023) {	/* 콘솔 종료 처리 */
				close_console(shtctl->sheets0 + (i - 768));
			} else if (1024 <= i && i <= 2023) {
				close_constask(taskctl->tasks0 + (i - 1024));
			} else if (2024 <= i && i <= 2279) {	/* 콘솔만을 닫는다 */
				sht2 = shtctl->sheets0 + (i - 2024);
				memman_free_4k(memman, (int) sht2->buf, 256 * 165);
				sheet_free(sht2);
			} else if (i == 2280) {	/* task manager를 닫는다 */
				close_taskmgr();
			}
		}
	}
}

void keywin_off(struct SHEET *key_win)
{
	change_wtitle8(key_win, 0);
	if ((key_win->flags & 0x20) != 0) {
		fifo32_put(&key_win->task->fifo, 3); /* 콘솔의 커서 OFF */
	}
	return;
}

void keywin_on(struct SHEET *key_win)
{
	change_wtitle8(key_win, 1);
	if ((key_win->flags & 0x20) != 0) {
		fifo32_put(&key_win->task->fifo, 2); /* 콘솔의 커서 ON */
	}
	return;
}

struct TASK *open_constask(struct SHEET *sht, unsigned int memtotal)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = task_alloc();
	struct TASK *parent = task_now();
	int *cons_fifo = (int *) memman_alloc_4k(memman, 128 * 4);
	task->cons_stack = memman_alloc_4k(memman, 64 * 1024);
	task->tss.esp = task->cons_stack + 64 * 1024 - 12;
	task->tss.eip = (int) &console_task;
	task->tss.es = 1 * 8;
	task->tss.cs = 2 * 8;
	task->tss.ss = 1 * 8;
	task->tss.ds = 1 * 8;
	task->tss.fs = 1 * 8;
	task->tss.gs = 1 * 8;
	task->time = 0;
	task->app_type = TASK_APP_CONSOLE;
	if (parent != 0 && parent->cons != 0) {
		task->cwd_clus = parent->cons->cwd_clus;
		strcpy(task->cwd_path, parent->cons->cwd_path);
	} else {
		task->cwd_clus = 0;
		strcpy(task->cwd_path, "/");
	}
	strcpy(task->name, "console");
	*((int *) (task->tss.esp + 4)) = (int) sht;
	*((int *) (task->tss.esp + 8)) = memtotal;
	task_run(task, 2, 2); /* level=2, priority=2 */
	fifo32_init(&task->fifo, 128, cons_fifo, task);
	return task;
}

struct SHEET *open_console(struct SHTCTL *shtctl, unsigned int memtotal)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct SHEET *sht = sheet_alloc(shtctl);
	unsigned char *buf = (unsigned char *) memman_alloc_4k(memman, 256 * 165); // 256 / 8 = 32, 165 / 16 = 10 .. 5
	struct SCROLLWIN *sw = (struct SCROLLWIN *) memman_alloc_4k(memman, sizeof(struct SCROLLWIN));
	sheet_setbuf(sht, buf, 256, 165, -1); /* 투명색없음 */
	make_window8(buf, 256, 165, "console", 0);
	make_textbox8(sht, 8, 28, 240, 128, COL8_000000);
	sht->scroll = sw;
	sht->flags |= SHEET_FLAG_SCROLLWIN;
	scrollwin_init(sw, sht, 8, 28, 240, 128, COL8_000000);
	make_textbox8(sht, 8, 28, 240, 128, COL8_000000);
	scrollwin_redraw(sw);
	sht->task = open_constask(sht, memtotal);
	sht->flags |= 0x20;	/* 커서 있음 */
	return sht;
}

/* console 윈도우 리사이즈 — 새 buffer 할당, frame/textbox 다시 그리고
 * sheet_resize 로 교체. 콘솔 task 에는 textbox 가 비워졌음을 별도 알리지
 * 않음 (이미 출력된 라인은 사라짐 — 추후 textbox 내부 backing store 가
 * 추가되면 그때 redraw 가능). 최소한 빈 콘솔로 정상 동작은 보장.        */
void console_resize(struct SHEET *sht, int new_w, int new_h)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct CONSOLE *cons;
	int old_w = sht->bxsize, old_h = sht->bysize;
	unsigned char *old_buf = sht->buf;
	unsigned int old_bytes = (unsigned int)(old_w * old_h);
	unsigned int new_bytes;
	unsigned char *new_buf;
	int tx0 = 8, ty0 = 28;
	int tw, th;

	if (new_w < RZ_MIN_W) new_w = RZ_MIN_W;
	if (new_h < RZ_MIN_H) new_h = RZ_MIN_H;
	tw = new_w - 16;       /* 좌우 8px 마진 */
	th = new_h - 28 - 9;   /* 상단 28(타이틀바), 하단 9 마진 */
	if (tw < 16)  tw = 16;
	if (th < 16)  th = 16;

	new_bytes = (unsigned int)(new_w * new_h);
	new_buf = (unsigned char *) memman_alloc_4k(memman, new_bytes);
	if (new_buf == 0) {
		return;	/* 메모리 부족 시 무시 */
	}

	/* 새 buffer 에 frame + textbox 그리기 */
	make_window8(new_buf, new_w, new_h, "console", 1);
	/* sheet_resize 후에 make_textbox8 가 sht->buf 를 쓰므로,
	 * 먼저 sheet_resize 를 호출해 sht->buf 를 new_buf 로 교체.            */
	sheet_resize(sht, new_buf, new_w, new_h);
	make_textbox8(sht, tx0, ty0, tw, th, COL8_000000);
	if (sht->scroll != 0) {
		sht->scroll->sht = sht;
		sht->scroll->x0 = tx0;
		sht->scroll->y0 = ty0;
		sht->scroll->wd = tw & ~7;
		sht->scroll->ht = th & ~15;
		scrollwin_scroll_to(sht->scroll, sht->scroll->scroll_top);
	}

	/* CONSOLE 구조체의 cur_x/y 가 textbox 밖으로 나가지 않게 reset.
	 * task->cons 는 console_task() 의 local 이라 외부에서 접근 불가하지만,
	 * task 구조체 안 cons 포인터로 접근 가능.                              */
	if (sht->task != 0 && sht->task->cons != 0) {
		cons = sht->task->cons;
		cons->cur_x = (sht->scroll != 0) ? scrollwin_cursor_x(sht->scroll) : 8;
		cons->cur_y = (sht->scroll != 0) ? scrollwin_cursor_y(sht->scroll) : 28;
		cons->width  = tw;
		cons->height = th;
	}

	sheet_refresh(sht, 0, 0, new_w, new_h);
	memman_free_4k(memman, (int) old_buf, old_bytes);
}

void scrollwin_window_resize(struct SHEET *sht, int new_w, int new_h, char *title)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	int old_w = sht->bxsize, old_h = sht->bysize;
	unsigned char *old_buf = sht->buf;
	unsigned int old_bytes = (unsigned int)(old_w * old_h);
	unsigned int new_bytes;
	unsigned char *new_buf;
	int tx0 = 8, ty0 = 28;
	int tw, th;

	if (new_w < RZ_MIN_W) new_w = RZ_MIN_W;
	if (new_h < RZ_MIN_H) new_h = RZ_MIN_H;
	tw = new_w - 16;
	th = new_h - 28 - 9;
	if (tw < 16 + SCROLLBAR_W) tw = 16 + SCROLLBAR_W;
	if (th < 16) th = 16;

	new_bytes = (unsigned int)(new_w * new_h);
	new_buf = (unsigned char *) memman_alloc_4k(memman, new_bytes);
	if (new_buf == 0) {
		return;
	}
	make_window8(new_buf, new_w, new_h, title, 1);
	sheet_resize(sht, new_buf, new_w, new_h);
	make_textbox8(sht, tx0, ty0, tw, th, sht->scroll->bc);
	if (sht->scroll != 0) {
		sht->scroll->sht = sht;
		sht->scroll->x0 = tx0;
		sht->scroll->y0 = ty0;
		sht->scroll->wd = tw & ~7;
		sht->scroll->ht = th & ~15;
		scrollwin_scroll_to(sht->scroll, sht->scroll->scroll_top);
	}
	sheet_refresh(sht, 0, 0, new_w, new_h);
	memman_free_4k(memman, (int) old_buf, old_bytes);
}

void close_constask(struct TASK *task)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	task_sleep(task);
	memman_free_4k(memman, task->cons_stack, 64 * 1024);
	memman_free_4k(memman, (int) task->fifo.buf, 128 * 4);
	task->flags = 0; /* task_free(task); 의 대신 */
	return;
}

void close_console(struct SHEET *sht)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = sht->task;
	if (sht->scroll != 0) {
		memman_free_4k(memman, (int) sht->scroll, sizeof(struct SCROLLWIN));
		sht->scroll = 0;
	}
	memman_free_4k(memman, (int) sht->buf, sht->bxsize * sht->bysize);
	sheet_free(sht);
	close_constask(task);
	return;
}

void open_taskmgr(unsigned int memtotal)
{
	int i;
	struct SHEET *sht;
	struct SHTCTL *ctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	if (taskmgr != 0) {
		for (i = 0; i < MAX_SHEETS; i++) {
			sht = &(ctl->sheets0[i]);
			if (sht->task == taskmgr) {
				keywin_on(sht);
			}
		}
		return;
	}
	taskmgr = task_alloc();
	int *tmgr_fifo = (int *) memman_alloc_4k(memman, 128 * 4);
	taskmgr->tss.esp = memman_alloc_4k(memman, 64 * 1024) + 64 * 1024 - 8;
	taskmgr->tss.eip = (int) &taskmgr_task;
	taskmgr->tss.es = 1 * 8;
	taskmgr->tss.cs = 2 * 8;
	taskmgr->tss.ss = 1 * 8;
	taskmgr->tss.ds = 1 * 8;
	taskmgr->tss.fs = 1 * 8;
	taskmgr->tss.gs = 1 * 8;
	taskmgr->time = 0;
	taskmgr->app_type = TASK_APP_SYSTEM;
	strcpy(taskmgr->name, "taskmgr");
	*((int *) (taskmgr->tss.esp + 4)) = memtotal;
	task_run(taskmgr, 2, 2); /* level=2, priority=2 */
	fifo32_init(&taskmgr->fifo, 128, tmgr_fifo, taskmgr);
	return;
}

void close_taskmgr(void)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	task_sleep(taskmgr);
	memman_free_4k(memman, (int) taskmgr->fifo.buf, 128 * 4);
	task_free(taskmgr);
	taskmgr = 0;
	return;
}
