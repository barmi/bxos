/* 콘솔 관계 */

#include "bootpack.h"
#include <stdio.h>
#include <string.h>

struct DBGWIN dbg;

static int cons_text_w(struct CONSOLE *cons);
static int cons_text_h(struct CONSOLE *cons);
static void cons_input_backspace(struct CONSOLE *cons, char *cmdline, int *cmd_len);
static void cons_input_clear(struct CONSOLE *cons, char *cmdline, int *cmd_len);
static void cons_input_set(struct CONSOLE *cons, char *cmdline, int *cmd_len, char *src);
static void cons_history_add(char history[CONS_HISTORY_MAX][CONS_CMDLINE_MAX],
		int *hist_count, char *cmdline);
static char *skip_spaces(char *p);
static int parse_decimal(char *p, int *out);
static void filehandle_close(struct MEMMAN *memman, struct FILEHANDLE *fh);
static struct FILEINFO *app_find(char *cmdline, char *app_name);
static void app_name_from_finfo(char *dst, struct FILEINFO *finfo);
static int app_subsystem(char *cmdline, int *fat);
static void task_set_app(struct TASK *task, char *name, int app_type);
static void task_reset_console(struct TASK *task);

#define FH_MODE_FREE	0
#define FH_MODE_READ	1
#define FH_MODE_WRITE	2

void console_task(struct SHEET *sheet, int memtotal)
{
	struct TASK *task = task_now();
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	int i, *fat = (int *) memman_alloc_4k(memman, 4 * 2880);
	struct CONSOLE cons;
	struct FILEHANDLE fhandle[8];
	char cmdline[CONS_CMDLINE_MAX];
	char history[CONS_HISTORY_MAX][CONS_CMDLINE_MAX];
	int cmd_len = 0, hist_count = 0, hist_pos = 0;
	unsigned char *nihongo = (char *) *((int *) 0x0fe8);

	cons.sht = sheet;
	cons.scroll = (sheet != 0) ? sheet->scroll : 0;
	cons.cur_x =  8;
	cons.cur_y = 28;
	cons.cur_c = -1;
	/* 초기 텍스트 영역 크기 — 기본 콘솔 윈도우(256×165) 의 textbox 와 일치.
	 * console_resize() 가 호출되면 이 값이 갱신되어 wrap/scroll 이 따라감.   */
	cons.width  = 240;
	cons.height = 128;
	task->cons = &cons;
	task->app_type = TASK_APP_CONSOLE;
	cmdline[0] = 0;
	task->cmdline = cmdline;

	if (cons.sht != 0) {
		cons.timer = timer_alloc();
		timer_init(cons.timer, &task->fifo, 1);
		timer_settime(cons.timer, 50);
	}
	file_readfat(fat, (unsigned char *) (ADR_DISKIMG + 0x000200));
	for (i = 0; i < 8; i++) {
		fhandle[i].buf = 0;
		fhandle[i].size = 0;
		fhandle[i].pos = 0;
		fhandle[i].mode = FH_MODE_FREE;
		fhandle[i].finfo = 0;
	}
	task->fhandle = fhandle;
	task->fat = fat;
	if (nihongo[4096] != 0xff) {	/* 일본어 폰트 파일을 읽어들일 수 있었는지?  */
		task->langmode = 1;
	} else {
		task->langmode = 0;
	}
	task->langbyte1 = 0;

	/* prompt 표시 */
	cons_putchar(&cons, '>', 1);

	for (;;) {
		io_cli();
		if (fifo32_status(&task->fifo) == 0) {
			task_sleep(task);
			io_sti();
		} else {
			i = fifo32_get(&task->fifo);
			io_sti();
			if (i <= 1 && cons.sht != 0) { /* 커서용 타이머 */
				if (i != 0) {
					timer_init(cons.timer, &task->fifo, 0); /* 다음은 0을 */
					if (cons.cur_c >= 0) {
						cons.cur_c = COL8_FFFFFF;
					}
				} else {
					timer_init(cons.timer, &task->fifo, 1); /* 다음은 1을 */
					if (cons.cur_c >= 0) {
						cons.cur_c = COL8_000000;
					}
				}
				timer_settime(cons.timer, 50);
			}
			if (i == 2) {	/* 커서 ON */
				cons.cur_c = COL8_FFFFFF;
			}
			if (i == 3) {	/* 커서 OFF */
				if (cons.sht != 0) {
					boxfill8(cons.sht->buf, cons.sht->bxsize, COL8_000000,
						cons.cur_x, cons.cur_y, cons.cur_x + 7, cons.cur_y + 15);
				}
				cons.cur_c = -1;
			}
			if (i == 4) {	/* 콘솔의 「×」버튼 클릭 */
				cmd_exit(&cons, fat);
			}
			if (256 <= i && i <= 511) { /* 키보드 데이터(태스크 A경유) */
				if (i == 8 + 256) {
					/* 백 스페이스 */
					cons_input_backspace(&cons, cmdline, &cmd_len);
				} else if (i == 10 + 256) {
					/* Enter */
					/* 스페이스로 지우고 나서 개행한다 */
					cons_putchar(&cons, ' ', 0);
					cmdline[cmd_len] = 0;
					cons_history_add(history, &hist_count, cmdline);
					hist_pos = hist_count;
					cons_newline(&cons);
					cons_runcmd(cmdline, &cons, fat, memtotal);	/* 커맨드 실행 */
					if (cons.sht == 0) {
						cmd_exit(&cons, fat);
					}
					/* prompt 표시 */
					cons_putchar(&cons, '>', 1);
					cmd_len = 0;
					cmdline[0] = 0;
				} else if (i == CONS_KEY_UP + 256) {
					if (hist_count > 0) {
						if (hist_pos > 0) {
							hist_pos--;
						}
						cons_input_set(&cons, cmdline, &cmd_len, history[hist_pos]);
					}
				} else if (i == CONS_KEY_DOWN + 256) {
					if (hist_count > 0) {
						if (hist_pos < hist_count - 1) {
							hist_pos++;
							cons_input_set(&cons, cmdline, &cmd_len, history[hist_pos]);
						} else {
							hist_pos = hist_count;
							cons_input_clear(&cons, cmdline, &cmd_len);
						}
					}
				} else {
					/* 일반 문자 */
					if (cons.cur_x < 8 + cons_text_w(&cons) - 8 &&
							cmd_len < CONS_CMDLINE_MAX - 1) {
						/* 한 글자 표시하고 나서, 커서를 1개 진행한다 */
						cmdline[cmd_len++] = i - 256;
						cmdline[cmd_len] = 0;
						cons_putchar(&cons, i - 256, 1);
					}
				}
			}
			/* 커서재표시 */
			if (cons.sht != 0) {
				if (cons.cur_c >= 0) {
					boxfill8(cons.sht->buf, cons.sht->bxsize, cons.cur_c, 
						cons.cur_x, cons.cur_y, cons.cur_x + 7, cons.cur_y + 15);
				}
				sheet_refresh(cons.sht, cons.cur_x, cons.cur_y, cons.cur_x + 8, cons.cur_y + 16);
			}
		}
	}
}

/*
 * cons_putchar / cons_newline 의 wrap/scroll 경계는 "안전한 sane 범위" 안의
 * cons->width / cons->height 값을 글자 셀(8×16) 단위로 내려 맞춰 사용한다.
 * 그렇지 않으면 원래의 240×128 hardcoded 값을 유지한다.
 *
 * 이렇게 한 이유: console_task() 진입 직후 ~ width/height 초기화 사이의
 * 짧은 시점이나, 다른 경로(taskmgr 등)에서 task->cons 가 partial-init 상태로
 * 참조될 때 garbage 가 들어 있으면 wrap 이 영원히 안 일어나서 텍스트가
 * textbox 경계를 침범하는 증상이 있었다. 16..4096 범위로 클램핑한다.
 */
static int cons_text_w(struct CONSOLE *cons)
{
	int w = cons->width;
	if (cons->scroll != 0) {
		return scrollwin_text_cols(cons->scroll) * 8;
	}
	if (w < 16 || w > 4096) {
		return 240;
	}
	w &= ~7;	/* glyph width = 8px */
	return (w >= 16) ? w : 16;
}
static int cons_text_h(struct CONSOLE *cons)
{
	int h = cons->height;
	if (cons->scroll != 0) {
		return cons->scroll->ht;
	}
	if (h < 16 || h > 4096) {
		return 128;
	}
	h &= ~15;	/* line height = 16px */
	return (h >= 16) ? h : 16;
}

static void cons_input_backspace(struct CONSOLE *cons, char *cmdline, int *cmd_len)
{
	if (*cmd_len <= 0) {
		return;
	}
	if (cons->scroll != 0) {
		scrollwin_backspace(cons->scroll);
		cons->cur_x = scrollwin_cursor_x(cons->scroll);
		cons->cur_y = scrollwin_cursor_y(cons->scroll);
	} else {
		cons_putchar(cons, ' ', 0);
		cons->cur_x -= 8;
	}
	(*cmd_len)--;
	cmdline[*cmd_len] = 0;
}

static void cons_input_clear(struct CONSOLE *cons, char *cmdline, int *cmd_len)
{
	while (*cmd_len > 0) {
		cons_input_backspace(cons, cmdline, cmd_len);
	}
}

static void cons_input_set(struct CONSOLE *cons, char *cmdline, int *cmd_len, char *src)
{
	int max_chars = cons_text_w(cons) / 8 - 2;
	if (max_chars > CONS_CMDLINE_MAX - 1) {
		max_chars = CONS_CMDLINE_MAX - 1;
	}
	cons_input_clear(cons, cmdline, cmd_len);
	while (*src != 0 && *cmd_len < max_chars) {
		cmdline[*cmd_len] = *src++;
		cons_putchar(cons, cmdline[*cmd_len], 1);
		(*cmd_len)++;
	}
	cmdline[*cmd_len] = 0;
}

static void cons_history_add(char history[CONS_HISTORY_MAX][CONS_CMDLINE_MAX],
		int *hist_count, char *cmdline)
{
	int i;
	if (cmdline[0] == 0) {
		return;
	}
	if (*hist_count > 0 &&
			strcmp(history[*hist_count - 1], cmdline) == 0) {
		return;
	}
	if (*hist_count == CONS_HISTORY_MAX) {
		for (i = 1; i < CONS_HISTORY_MAX; i++) {
			strcpy(history[i - 1], history[i]);
		}
		(*hist_count)--;
	}
	strcpy(history[*hist_count], cmdline);
	(*hist_count)++;
}

void cons_putchar(struct CONSOLE *cons, int chr, char move)
{
	char s[2];
	int W = cons_text_w(cons);
	int H = cons_text_h(cons);
	if (cons->scroll != 0) {
		if (move != 0 || chr == 0x0a || chr == 0x09) {
			scrollwin_putc(cons->scroll, chr, COL8_FFFFFF);
		}
		cons->cur_x = scrollwin_cursor_x(cons->scroll);
		cons->cur_y = scrollwin_cursor_y(cons->scroll);
		return;
	}
	if (cons->cur_y > 28 + H - 16) {
		cons->cur_y = 28 + H - 16;
	}
	s[0] = chr;
	s[1] = 0;
	if (s[0] == 0x09) {	/* 탭 */
		for (;;) {
			if (cons->sht != 0) {
				putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, " ", 1);
			}
			cons->cur_x += 8;
			if (cons->cur_x >= 8 + W) {
				cons_newline(cons);
			}
			if (((cons->cur_x - 8) & 0x1f) == 0) {
				break;	/* 32로 나누어 떨어지면 break */
			}
		}
	} else if (s[0] == 0x0a) {	/* 개행 */
		cons_newline(cons);
	} else if (s[0] == 0x0d) {	/* 복귀 */
		/* 우선 아무것도 하지 않는다 */
	} else {	/* 보통 문자 */
		if (cons->sht != 0) {
			putfonts8_asc_sht(cons->sht, cons->cur_x, cons->cur_y, COL8_FFFFFF, COL8_000000, s, 1);
		}
		if (move != 0) {
			/* move가 0일 때는 커서를 진행시키지 않는다 */
			cons->cur_x += 8;
			if (cons->cur_x >= 8 + W) {
				cons_newline(cons);
			}
		}
	}
	return;
}

void cons_newline(struct CONSOLE *cons)
{
	int x, y;
	struct SHEET *sheet = cons->sht;
	struct TASK *task = task_now();
	int W = cons_text_w(cons);
	int H = cons_text_h(cons);
	if (cons->scroll != 0) {
		scrollwin_putc(cons->scroll, '\n', COL8_FFFFFF);
		cons->cur_x = scrollwin_cursor_x(cons->scroll);
		cons->cur_y = scrollwin_cursor_y(cons->scroll);
		return;
	}
	if (cons->cur_y < 28 + H - 16) {
		cons->cur_y += 16; /* 다음 행에 */
	} else {
		/* 스크롤 */
		if (sheet != 0) {
			for (y = 28; y < 28 + H - 16; y++) {
				for (x = 8; x < 8 + W; x++) {
					sheet->buf[x + y * sheet->bxsize] = sheet->buf[x + (y + 16) * sheet->bxsize];
				}
			}
			for (y = 28 + H - 16; y < 28 + H; y++) {
				for (x = 8; x < 8 + W; x++) {
					sheet->buf[x + y * sheet->bxsize] = COL8_000000;
				}
			}
			sheet_refresh(sheet, 8, 28, 8 + W, 28 + H);
		}
	}
	cons->cur_x = 8;
	if (task->langmode == 1 && task->langbyte1 != 0) {
		cons->cur_x = 16;
	}
	return;
}

void cons_putstr0(struct CONSOLE *cons, char *s)
{
	for (; *s != 0; s++) {
		cons_putchar(cons, *s, 1);
	}
	return;
}

void cons_putstr1(struct CONSOLE *cons, char *s, int l)
{
	int i;
	for (i = 0; i < l; i++) {
		cons_putchar(cons, s[i], 1);
	}
	return;
}

void cons_runcmd(char *cmdline, struct CONSOLE *cons, int *fat, int memtotal)
{
	if (strcmp(cmdline, "mem") == 0 && cons->sht != 0) {
		cmd_mem(cons, memtotal);
	} else if (strcmp(cmdline, "cls") == 0 && cons->sht != 0) {
		cmd_cls(cons);
	} else if (strcmp(cmdline, "dir") == 0 && cons->sht != 0) {
		cmd_dir(cons, cmdline);
	} else if (strcmp(cmdline, "task") == 0) {
		cmd_task();
	} else if (strcmp(cmdline, "disk") == 0 && cons->sht != 0) {
		cmd_disk(cons);
	} else if (strncmp(cmdline, "touch ", 6) == 0 && cons->sht != 0) {
		cmd_touch(cons, cmdline);
	} else if (strncmp(cmdline, "rm ", 3) == 0 && cons->sht != 0) {
		cmd_rm(cons, cmdline);
	} else if (strncmp(cmdline, "echo ", 5) == 0 && cons->sht != 0) {
		cmd_echo(cons, cmdline);
	} else if (strncmp(cmdline, "mkfile ", 7) == 0 && cons->sht != 0) {
		cmd_mkfile(cons, cmdline);
	} else if (strcmp(cmdline, "exit") == 0) {
		cmd_exit(cons, fat);
	} else if (strncmp(cmdline, "start ", 6) == 0) {
		cmd_start(cons, cmdline, fat, memtotal);
	} else if (strncmp(cmdline, "ncst ", 5) == 0) {
		cmd_ncst(cons, cmdline, memtotal);
	} else if (strncmp(cmdline, "langmode ", 9) == 0) {
		cmd_langmode(cons, cmdline);
	} else if (strncmp(cmdline, "taskmgr", 7) == 0) {
		open_taskmgr(memtotal);
	} else if (cmdline[0] != 0) {
		if (cmd_app(cons, fat, cmdline) == 0) {
			/* 커맨드도 아니고, 어플리케이션도 아니고, 빈 행도 아니다 */
			cons_putstr0(cons, "Bad command or file name.\n\n");
		}
	}
	return;
}

static char *skip_spaces(char *p)
{
	while (*p == ' ') {
		p++;
	}
	return p;
}

static int parse_decimal(char *p, int *out)
{
	int v = 0, n = 0;
	p = skip_spaces(p);
	while ('0' <= *p && *p <= '9') {
		v = v * 10 + (*p - '0');
		p++;
		n++;
	}
	if (n == 0 || *skip_spaces(p) != 0) {
		return -1;
	}
	*out = v;
	return 0;
}

static void filehandle_close(struct MEMMAN *memman, struct FILEHANDLE *fh)
{
	if (fh == 0 || fh->mode == FH_MODE_FREE) {
		return;
	}
	if (fh->mode == FH_MODE_READ && fh->buf != 0 && fh->size > 0) {
		memman_free_4k(memman, (int) fh->buf, fh->size);
	}
	fh->buf = 0;
	fh->size = 0;
	fh->pos = 0;
	fh->mode = FH_MODE_FREE;
	fh->finfo = 0;
	return;
}

void cmd_mem(struct CONSOLE *cons, int memtotal)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	char s[60];
	sprintf(s, "total   %dMB\nfree %dKB\n\n", memtotal / (1024 * 1024), memman_total(memman) / 1024);
	cons_putstr0(cons, s);
	return;
}

void cmd_cls(struct CONSOLE *cons)
{
	int x, y;
	struct SHEET *sheet = cons->sht;
	int W = cons_text_w(cons);
	int H = cons_text_h(cons);
	if (cons->scroll != 0) {
		scrollwin_init(cons->scroll, sheet, cons->scroll->x0, cons->scroll->y0,
				cons->scroll->wd, cons->scroll->ht, COL8_000000);
		cons->cur_x = scrollwin_cursor_x(cons->scroll);
		cons->cur_y = scrollwin_cursor_y(cons->scroll);
		return;
	}
	for (y = 28; y < 28 + H; y++) {
		for (x = 8; x < 8 + W; x++) {
			sheet->buf[x + y * sheet->bxsize] = COL8_000000;
		}
	}
	sheet_refresh(sheet, 8, 28, 8 + W, 28 + H);
	cons->cur_y = 28;
	return;
}

void cmd_dir(struct CONSOLE *cons, char *cmdline)
{
	/* work1 Phase 3: 데이터 드라이브(g_data_mount) 의 루트 디렉터리를 표시. */
	struct FILEINFO *finfo = fs_data_root();
	int max = fs_data_root_max();
	int i, j;
	char s[30];
	if (finfo == 0) {
		cons_putstr0(cons, "(no data disk mounted)\n\n");
		return;
	}
	for (i = 0; i < max; i++) {
		if (finfo[i].name[0] == 0x00) {
			break;
		}
		if (finfo[i].name[0] != 0xe5) {
			if ((finfo[i].type & 0x18) == 0) {
				sprintf(s, "filename.ext   %7d\n", finfo[i].size);
				for (j = 0; j < 8; j++) {
					s[j] = finfo[i].name[j];
				}
				s[ 9] = finfo[i].ext[0];
				s[10] = finfo[i].ext[1];
				s[11] = finfo[i].ext[2];
				cons_putstr0(cons, s);
				//s[22] = ' ';
				dbg_putstr0(s,COL8_FFFFFF);
			}
		}
	}
	cons_newline(cons);
	return;
}

void cmd_task(void)
{
	int i;
	char msg[40];
	dbg_putstr0("ID  TY  NAME         LV  TIME      \n", COL8_FFFFFF);
	dbg_putstr0("--- --- ------------ --- ----------\n", COL8_FFFFFF);
	for (i = 0; i < taskctl->alloc; i++) {
		if (taskctl->tasks0[i].flags == 0) {
			continue;
		}
		memset(msg, 0, sizeof(msg));
		sprintf(msg, "%3d %-3s %-12s   %1d %7d.%02d\n", i,
			taskctl->tasks0[i].app_type == TASK_APP_WINDOW ? "WIN" :
			taskctl->tasks0[i].app_type == TASK_APP_CONSOLE ? "CON" : "SYS",
			taskctl->tasks0[i].name, taskctl->tasks0[i].level,
			taskctl->tasks0[i].time / 100, taskctl->tasks0[i].time % 100);
		dbg_putstr0(msg, COL8_FFFFFF);
	}
	return;
}

void cmd_disk(struct CONSOLE *cons)
{
	/* work1 Phase 2: ATA 드라이브 정보 출력. 부팅 시 캐시한 결과를 표시한다. */
	char s[80];
	int d;
	for (d = 0; d < 2; d++) {
		struct ATA_INFO *info = &ata_drive_info[d];
		if (!info->present) {
			sprintf(s, "ata%d: (not present)\n", d);
			cons_putstr0(cons, s);
			continue;
		}
		sprintf(s, "ata%d: %s\n", d, info->model);
		cons_putstr0(cons, s);
		sprintf(s, "      sectors=%d  size=%dMB\n",
				info->total_sectors,
				info->total_sectors / 2 / 1024);
		cons_putstr0(cons, s);

		/* 부트섹터(LBA 0) 한 섹터 read 검증. BPB 의 OEM 문자열과
		 * boot sig 0xAA55 만 확인한다 (저장은 안 함). */
		{
			unsigned char buf[512];
			int r = ata_read_sectors(d, 0, 1, buf);
			if (r != 1) {
				sprintf(s, "      read LBA0: FAIL (%d)\n", r);
				cons_putstr0(cons, s);
			} else {
				char oem[9];
				int i;
				for (i = 0; i < 8; i++) oem[i] = buf[3 + i];
				oem[8] = 0;
				sprintf(s, "      LBA0 oem='%s' sig=%02x%02x\n",
						oem, buf[511], buf[510]);
				cons_putstr0(cons, s);
			}
		}
	}
	cons_newline(cons);
	return;
}

void cmd_touch(struct CONSOLE *cons, char *cmdline)
{
	char *name = skip_spaces(cmdline + 6);
	int r;

	if (*name == 0) {
		cons_putstr0(cons, "usage: touch <file>\n\n");
		return;
	}
	if (fs_data_search(name) != 0) {
		cons_newline(cons);
		return;
	}
	r = fs_data_create(name);
	if (r != 0) {
		cons_putstr0(cons, "touch failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_rm(struct CONSOLE *cons, char *cmdline)
{
	char *name = skip_spaces(cmdline + 3);
	struct FILEINFO *finfo;

	if (*name == 0) {
		cons_putstr0(cons, "usage: rm <file>\n\n");
		return;
	}
	finfo = fs_data_search(name);
	if (finfo == 0) {
		cons_putstr0(cons, "File not found.\n\n");
		return;
	}
	if (fs_data_unlink(finfo) != 0) {
		cons_putstr0(cons, "rm failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_echo(struct CONSOLE *cons, char *cmdline)
{
	char *text = cmdline + 5;
	char *gt = 0, *name;
	char data[CONS_CMDLINE_MAX];
	struct FILEINFO *finfo;
	int i, len, r;

	for (i = 0; text[i] != 0; i++) {
		if (text[i] == '>') {
			gt = &text[i];
			break;
		}
	}
	if (gt == 0) {
		cons_putstr0(cons, "usage: echo <text> > <file>\n\n");
		return;
	}
	name = skip_spaces(gt + 1);
	if (*name == 0) {
		cons_putstr0(cons, "usage: echo <text> > <file>\n\n");
		return;
	}
	while (gt > text && gt[-1] == ' ') {
		gt--;
	}
	len = gt - text;
	if (len >= CONS_CMDLINE_MAX - 1) {
		len = CONS_CMDLINE_MAX - 2;
	}
	for (i = 0; i < len; i++) {
		data[i] = text[i];
	}
	data[len++] = '\n';

	finfo = fs_data_search(name);
	if (finfo == 0) {
		r = fs_data_create(name);
		if (r != 0) {
			cons_putstr0(cons, "echo failed.\n\n");
			return;
		}
		finfo = fs_data_search(name);
	}
	if (finfo == 0 || fs_data_truncate(finfo, 0) != 0 ||
			fs_data_write(finfo, 0, data, len) != len) {
		cons_putstr0(cons, "echo failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_mkfile(struct CONSOLE *cons, char *cmdline)
{
	char *name = skip_spaces(cmdline + 7);
	char *p = name;
	char chunk[512];
	struct FILEINFO *finfo;
	int size, pos = 0, n, i, r;

	while (*p > ' ') {
		p++;
	}
	if (*name == 0 || *p == 0) {
		cons_putstr0(cons, "usage: mkfile <file> <bytes>\n\n");
		return;
	}
	*p++ = 0;
	if (parse_decimal(p, &size) != 0 || size < 0) {
		cons_putstr0(cons, "usage: mkfile <file> <bytes>\n\n");
		return;
	}

	finfo = fs_data_search(name);
	if (finfo == 0) {
		r = fs_data_create(name);
		if (r != 0) {
			cons_putstr0(cons, "mkfile failed.\n\n");
			return;
		}
		finfo = fs_data_search(name);
	}
	if (finfo == 0 || fs_data_truncate(finfo, 0) != 0) {
		cons_putstr0(cons, "mkfile failed.\n\n");
		return;
	}
	while (pos < size) {
		n = size - pos;
		if (n > (int) sizeof chunk) {
			n = sizeof chunk;
		}
		for (i = 0; i < n; i++) {
			chunk[i] = 'A' + ((pos + i) % 26);
		}
		if (fs_data_write(finfo, pos, chunk, n) != n) {
			cons_putstr0(cons, "mkfile failed.\n\n");
			return;
		}
		pos += n;
	}
	cons_newline(cons);
	return;
}

void cmd_exit(struct CONSOLE *cons, int *fat)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = task_now();
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct FIFO32 *fifo = (struct FIFO32 *) *((int *) 0x0fec);
	if (cons->sht != 0) {
		timer_cancel(cons->timer);
	}
	memman_free_4k(memman, (int) fat, 4 * 2880);
	io_cli();
	if (cons->sht != 0) {
		fifo32_put(fifo, cons->sht - shtctl->sheets0 + 768);	/* 768~1023 */
	} else {
		fifo32_put(fifo, task - taskctl->tasks0 + 1024);	/* 1024~2023 */
	}
	io_sti();
	for (;;) {
		task_sleep(task);
	}
}

void cmd_start(struct CONSOLE *cons, char *cmdline, int *fat, int memtotal)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht = 0;
	struct TASK *task;
	struct FIFO32 *fifo;
	int i, subsystem = app_subsystem(cmdline + 6, fat);
	if (subsystem == HE2_SUBSYSTEM_WINDOW) {
		task = open_constask(0, memtotal);
	} else {
		sht = open_console(shtctl, memtotal);
		task = sht->task;
		sheet_slide(sht, 32, 4);
		sheet_updown(sht, shtctl->top);
	}
	fifo = &task->fifo;
	/* 커맨드 라인에 입력된 문자열을, 한 글자씩 새로운 콘솔에 입력 */
	for (i = 6; cmdline[i] != 0; i++) {
		fifo32_put(fifo, cmdline[i] + 256);
	}
	fifo32_put(fifo, 10 + 256);	/* Enter */
	cons_newline(cons);
	return;
}

void cmd_ncst(struct CONSOLE *cons, char *cmdline, int memtotal)
{
	struct TASK *task = open_constask(0, memtotal);
	struct FIFO32 *fifo = &task->fifo;
	int i;
	/* 커맨드 라인에 입력된 문자열을, 한 글자씩 새로운 콘솔에 입력 */
	for (i = 5; cmdline[i] != 0; i++) {
		fifo32_put(fifo, cmdline[i] + 256);
	}
	fifo32_put(fifo, 10 + 256);	/* Enter */
	cons_newline(cons);
	return;
}

void cmd_langmode(struct CONSOLE *cons, char *cmdline)
{
	struct TASK *task = task_now();
	unsigned char mode = cmdline[9] - '0';
	if (mode <= 2) {
		task->langmode = mode;
	} else {
		cons_putstr0(cons, "mode number error.\n");
	}
	cons_newline(cons);
	return;
}

/*
 * HE2 (Haribote Executable v2) 헤더 — he2/libbxos/include/he2.h 와 동기.
 * 커널은 stdint 가 없어 명시적 정수 타입을 쓴다.
 */
struct he2_hdr_kern {
	char			magic[4];		/* "HE2\0"            */
	unsigned short	version;		/* 1                  */
	unsigned short	header_size;	/* 32                 */
	unsigned int	entry_off;		/* DS-relative entry  */
	unsigned int	image_size;		/* file image size    */
	unsigned int	bss_size;		/* zero-fill after image */
	unsigned int	stack_size;
	unsigned int	heap_size;
	unsigned int	flags;
};

static struct FILEINFO *app_find(char *cmdline, char *app_name)
{
	struct FILEINFO *finfo;
	char name[18];
	int i, has_dot = 0;

	for (i = 0; i < 13; i++) {
		if (cmdline[i] <= ' ') {
			break;
		}
		if (cmdline[i] == '.') {
			has_dot = 1;
		}
		name[i] = cmdline[i];
	}
	name[i] = 0;
	if (i == 0) {
		return 0;
	}

	finfo = fs_data_search(name);
	if (finfo == 0 && has_dot == 0) {
		name[i    ] = '.';
		name[i + 1] = 'H';
		name[i + 2] = 'E';
		name[i + 3] = '2';
		name[i + 4] = 0;
		finfo = fs_data_search(name);
		if (finfo == 0) {
			name[i + 1] = 'H';
			name[i + 2] = 'R';
			name[i + 3] = 'B';
			finfo = fs_data_search(name);
		}
	}
	if (finfo != 0 && app_name != 0) {
		app_name_from_finfo(app_name, finfo);
	}
	return finfo;
}

static void app_name_from_finfo(char *dst, struct FILEINFO *finfo)
{
	int i, j = 0, ext_nonblank = 0;
	for (i = 0; i < 8 && finfo->name[i] != ' '; i++) {
		dst[j++] = finfo->name[i];
	}
	for (i = 0; i < 3; i++) {
		if (finfo->ext[i] != ' ') {
			ext_nonblank = 1;
		}
	}
	if (ext_nonblank != 0 && j < 14) {
		dst[j++] = '.';
		for (i = 0; i < 3 && finfo->ext[i] != ' ' && j < 15; i++) {
			dst[j++] = finfo->ext[i];
		}
	}
	dst[j] = 0;
}

static int app_subsystem(char *cmdline, int *fat)
{
	struct FILEINFO *finfo;
	char *p;
	int appsiz, subsystem = HE2_SUBSYSTEM_CONSOLE;
	struct he2_hdr_kern *h;

	finfo = app_find(cmdline, 0);
	if (finfo == 0) {
		return HE2_SUBSYSTEM_CONSOLE;
	}
	appsiz = finfo->size;
	p = fs_data_loadfile(finfo->clustno, &appsiz);
	if (p == 0) {
		return HE2_SUBSYSTEM_CONSOLE;
	}
	if (appsiz >= 32 && p[0] == 'H' && p[1] == 'E' && p[2] == '2' && p[3] == 0) {
		h = (struct he2_hdr_kern *) p;
		subsystem = h->flags & HE2_FLAG_SUBSYSTEM_MASK;
	}
	memman_free_4k((struct MEMMAN *) MEMMAN_ADDR, (int) p, appsiz);
	return subsystem;
}

static void task_set_app(struct TASK *task, char *name, int app_type)
{
	int i;
	for (i = 0; i < 15 && name[i] != 0; i++) {
		task->name[i] = name[i];
	}
	task->name[i] = 0;
	task->app_type = app_type;
}

static void task_reset_console(struct TASK *task)
{
	strcpy(task->name, "console");
	task->app_type = TASK_APP_CONSOLE;
}

static int load_and_run_he2(struct CONSOLE *cons, struct TASK *task,
		struct MEMMAN *memman, char *p, int appsiz, char *app_name)
{
	struct he2_hdr_kern *h = (struct he2_hdr_kern *) p;
	unsigned int total_sz, image_sz, bss_sz, heap_sz, stack_sz;
	int i, j;
	char *q;
	struct SHTCTL *shtctl;
	struct SHEET *sht;

	if ((unsigned) appsiz < sizeof(struct he2_hdr_kern)) {
		cons_putstr0(cons, ".he2 too small.\n");
		return 0;
	}
	if (h->header_size < sizeof(struct he2_hdr_kern)) {
		cons_putstr0(cons, ".he2 header_size invalid.\n");
		return 0;
	}
	if (h->image_size != (unsigned int) appsiz) {
		cons_putstr0(cons, ".he2 image_size mismatch.\n");
		return 0;
	}
	if (h->entry_off < h->header_size || h->entry_off >= h->image_size) {
		cons_putstr0(cons, ".he2 entry_off out of range.\n");
		return 0;
	}

	image_sz = h->image_size;
	bss_sz   = h->bss_size;
	heap_sz  = h->heap_size;
	stack_sz = h->stack_size ? h->stack_size : (16 * 1024);

	total_sz = image_sz + bss_sz + heap_sz + stack_sz;
	total_sz = (total_sz + 0x0FFFu) & ~0x0FFFu;	/* 4K round-up */

	q = (char *) memman_alloc_4k(memman, total_sz);
	if (q == 0) {
		cons_putstr0(cons, ".he2 out of memory.\n");
		return 0;
	}
	task_set_app(task, app_name,
			((h->flags & HE2_FLAG_SUBSYSTEM_MASK) == HE2_SUBSYSTEM_WINDOW) ?
			TASK_APP_WINDOW : TASK_APP_CONSOLE);
	task->ds_base = (int) q;

	/* 코드와 데이터 양쪽 모두 같은 영역(q)을 가리키게 한다.        */
	set_segmdesc(task->ldt + 0, total_sz - 1, (int) q, AR_CODE32_ER + 0x60);
	set_segmdesc(task->ldt + 1, total_sz - 1, (int) q, AR_DATA32_RW + 0x60);

	/* file image 복사 */
	for (i = 0; i < (int) image_sz; i++) {
		q[i] = p[i];
	}
	/* BSS + heap 영역 zero-fill (스택은 미초기화로 둠) */
	for (j = i; j < (int) (image_sz + bss_sz + heap_sz); j++) {
		q[j] = 0;
	}

	/* CS:EIP = 0:entry_off, ESP = total_sz (스택 top) */
	start_app(h->entry_off, 0 * 8 + 4, total_sz, 1 * 8 + 4,
			&(task->tss.esp0));

	task_reset_console(task);
	shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	for (i = 0; i < MAX_SHEETS; i++) {
		sht = &(shtctl->sheets0[i]);
		if ((sht->flags & 0x11) == 0x11 && sht->task == task) {
			sheet_free(sht);
		}
	}
	for (i = 0; i < 8; i++) {
		filehandle_close(memman, &task->fhandle[i]);
	}
	timer_cancelall(&task->fifo);
	memman_free_4k(memman, (int) q, total_sz);
	task->langbyte1 = 0;
	return 1;
}

int cmd_app(struct CONSOLE *cons, int *fat, char *cmdline)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct FILEINFO *finfo;
	char app_name[16], *p, *q;
	struct TASK *task = task_now();
	int i, segsiz, datsiz, esp, dathrb, appsiz;
	struct SHTCTL *shtctl;
	struct SHEET *sht;

	/* 파일을 찾는다 */
	finfo = app_find(cmdline, app_name);

	if (finfo != 0) {
		/* 파일이 발견되었을 경우 */
		appsiz = finfo->size;
		p = fs_data_loadfile(finfo->clustno, &appsiz);
		if (p == 0) {
			cons_putstr0(cons, "out of memory.\n\n");
			return 1;
		}

		/* HE2 인지 먼저 검사 (magic = "HE2\0") */
		if (appsiz >= 32 && p[0] == 'H' && p[1] == 'E' && p[2] == '2' && p[3] == 0) {
			load_and_run_he2(cons, task, memman, p, appsiz, app_name);
		} else if (appsiz >= 36 && strncmp(p + 4, "Hari", 4) == 0 && *p == 0x00) {
			segsiz = *((int *) (p + 0x0000));
			esp    = *((int *) (p + 0x000c));
			datsiz = *((int *) (p + 0x0010));
			dathrb = *((int *) (p + 0x0014));
			q = (char *) memman_alloc_4k(memman, segsiz);
			if (q == 0) {
				cons_putstr0(cons, "out of memory.\n");
				memman_free_4k(memman, (int) p, appsiz);
				cons_newline(cons);
				return 1;
			}
			task->ds_base = (int) q;
			set_segmdesc(task->ldt + 0, appsiz - 1, (int) p, AR_CODE32_ER + 0x60);
			set_segmdesc(task->ldt + 1, segsiz - 1, (int) q, AR_DATA32_RW + 0x60);
			for (i = 0; i < datsiz; i++) {
				q[esp + i] = p[dathrb + i];
			}
			task_set_app(task, app_name, TASK_APP_CONSOLE);
			start_app(0x1b, 0 * 8 + 4, esp, 1 * 8 + 4, &(task->tss.esp0));
			task_reset_console(task);
			shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
			for (i = 0; i < MAX_SHEETS; i++) {
				sht = &(shtctl->sheets0[i]);
				if ((sht->flags & 0x11) == 0x11 && sht->task == task) {
					/* 어플리케이션 open & 레이어 발견 */
					sheet_free(sht);	/* 닫는다 */
				}
			}
			for (i = 0; i < 8; i++) {	/* 클로우즈 하지 않는 파일을 클로우즈 */
				filehandle_close(memman, &task->fhandle[i]);
			}
			timer_cancelall(&task->fifo);
			memman_free_4k(memman, (int) q, segsiz);
			task->langbyte1 = 0;
		} else {
			cons_putstr0(cons, "exec format error (need .he2 or .hrb).\n");
		}
		memman_free_4k(memman, (int) p, appsiz);
		cons_newline(cons);
		return 1;
	}
	/* 파일이 발견되지 않았을 경우 */
	return 0;
}

int *hrb_api(int *reg, int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax)
{
	struct TASK *task = task_now();
	int ds_base = task->ds_base;
	struct CONSOLE *cons = task->cons;
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht;
	struct FIFO32 *sys_fifo = (struct FIFO32 *) *((int *) 0x0fec);
		/* 보존을 위한 PUSHAD 프레임을 수정 */
		/* reg[0] : EDI,   reg[1] : ESI,   reg[2] : EBP,   reg[3] : ESP */
		/* reg[4] : EBX,   reg[5] : EDX,   reg[6] : ECX,   reg[7] : EAX */
	int i;
	struct FILEINFO *finfo;
	struct FILEHANDLE *fh;
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;

	if (edx == 1) {
		// api_putchar
		cons_putchar(cons, eax & 0xff, 1);
	} else if (edx == 2) {
		// api_putstr0
		cons_putstr0(cons, (char *) ebx + ds_base);
	} else if (edx == 3) {
		// api_putstr1
		cons_putstr1(cons, (char *) ebx + ds_base, ecx);
	} else if (edx == 4) {
		// api_end
		return &(task->tss.esp0);
	} else if (edx == 5) {
		// api_openwin
		task->app_type = TASK_APP_WINDOW;
		sht = sheet_alloc(shtctl);
		sht->task = task;
		sht->flags |= 0x10;	/* 어플리케이션이 만든 윈도우           */
		sht->flags &= ~SHEET_FLAG_RESIZABLE; /* 앱 buffer 고정크기 → 리사이즈 금지 */
		sheet_setbuf(sht, (char *) ebx + ds_base, esi, edi, eax);
		make_window8((char *) ebx + ds_base, esi, edi, (char *) ecx + ds_base, 0);
		sheet_slide(sht, ((shtctl->xsize - esi) / 2) & ~3, (shtctl->ysize - edi) / 2);
		sheet_updown(sht, shtctl->top); /* 지금의 마우스와 같은 높이가 되도록 지정： 마우스는 이 위가 된다 */
		reg[7] = (int) sht;
	} else if (edx == 6) {
		// api_putstrwin
		sht = (struct SHEET *) (ebx & 0xfffffffe);
		putfonts8_asc(sht->buf, sht->bxsize, esi, edi, eax, (char *) ebp + ds_base);
		if ((ebx & 1) == 0) {
			sheet_refresh(sht, esi, edi, esi + ecx * 8, edi + 16);
		}
	} else if (edx == 7) {
		// api_boxfilwin
		sht = (struct SHEET *) (ebx & 0xfffffffe);
		boxfill8(sht->buf, sht->bxsize, ebp, eax, ecx, esi, edi);
		if ((ebx & 1) == 0) {
			sheet_refresh(sht, eax, ecx, esi + 1, edi + 1);
		}
	} else if (edx == 8) {
		// api_initmalloc
		memman_init((struct MEMMAN *) (ebx + ds_base));
		ecx &= 0xfffffff0;	/* 16바이트 단위로 */
		memman_free((struct MEMMAN *) (ebx + ds_base), eax, ecx);
	} else if (edx == 9) {
		// api_malloc
		ecx = (ecx + 0x0f) & 0xfffffff0; /* 16바이트 단위로 절상 */
		reg[7] = memman_alloc((struct MEMMAN *) (ebx + ds_base), ecx);
	} else if (edx == 10) {
		// api_free
		ecx = (ecx + 0x0f) & 0xfffffff0; /* 16바이트 단위로 절상 */
		memman_free((struct MEMMAN *) (ebx + ds_base), eax, ecx);
	} else if (edx == 11) {
		// api_point
		sht = (struct SHEET *) (ebx & 0xfffffffe);
		sht->buf[sht->bxsize * edi + esi] = eax;
		if ((ebx & 1) == 0) {
			sheet_refresh(sht, esi, edi, esi + 1, edi + 1);
		}
	} else if (edx == 12) {
		// api_refreshwin
		sht = (struct SHEET *) (ebx & 0xfffffffe);
		sheet_refresh(sht, eax, ecx, esi, edi);
	} else if (edx == 13) {
		// api_linewin
		sht = (struct SHEET *) (ebx & 0xfffffffe);
		hrb_api_linewin(sht, eax, ecx, esi, edi, ebp);
		if ((ebx & 1) == 0) {
			if (eax > esi) {
				i = eax;
				eax = esi;
				esi = i;
			}
			if (ecx > edi) {
				i = ecx;
				ecx = edi;
				edi = i;
			}
			sheet_refresh(sht, eax, ecx, esi + 1, edi + 1);
		}
	} else if (edx == 14) {
		// api_closewin
		sheet_free((struct SHEET *) ebx);
	} else if (edx == 15) {
		// api_getkey
		for (;;) {
			io_cli();
			if (fifo32_status(&task->fifo) == 0) {
				if (eax != 0) {
					task_sleep(task);	/* FIFO가 비었으므로 기다린다 */
				} else {
					io_sti();
					reg[7] = -1;
					return 0;
				}
			}
			i = fifo32_get(&task->fifo);
			io_sti();
			if (i <= 1 && cons->sht != 0) { /* 커서용 타이머 */
				/* 어플리케이션 실행중은 커서가 나오지 않기 때문에, 표시용 1을 call */
				timer_init(cons->timer, &task->fifo, 1); /* 다음은 1을 */
				timer_settime(cons->timer, 50);
			}
			if (i == 2) {	/* 커서 ON */
				cons->cur_c = COL8_FFFFFF;
			}
			if (i == 3) {	/* 커서 OFF */
				cons->cur_c = -1;
			}
			if (i == 4) {	/* 콘솔만을 닫는다 */
				timer_cancel(cons->timer);
				io_cli();
				fifo32_put(sys_fifo, cons->sht - shtctl->sheets0 + 2024);	/* 2024~2279 */
				cons->sht = 0;
				io_sti();
			}
			if (i >= 256) { /* 키보드 데이터(태스크 A경유) 등 */
				reg[7] = i - 256;
				return 0;
			}
		}
	} else if (edx == 16) {
		// api_alloctimer
		reg[7] = (int) timer_alloc();
		((struct TIMER *) reg[7])->flags2 = 1;	/* 자동 캔슬 유효 */
	} else if (edx == 17) {
		// api_inittimer
		timer_init((struct TIMER *) ebx, &task->fifo, eax + 256);
	} else if (edx == 18) {
		// api_settimer
		timer_settime((struct TIMER *) ebx, eax);
	} else if (edx == 19) {
		// api_freetimer
		timer_free((struct TIMER *) ebx);
	} else if (edx == 20) {
		// api_beep
		if (eax == 0) {
			i = io_in8(0x61);
			io_out8(0x61, i & 0x0d);
		} else {
			i = 1193180000 / eax;
			io_out8(0x43, 0xb6);
			io_out8(0x42, i & 0xff);
			io_out8(0x42, i >> 8);
			i = io_in8(0x61);
			io_out8(0x61, (i | 0x03) & 0x0f);
		}
	} else if (edx == 21) {
		// api_fopen
		for (i = 0; i < 8; i++) {
			if (task->fhandle[i].mode == FH_MODE_FREE) {
				break;
			}
		}
		reg[7] = 0;
		if (i < 8) {
			/* work1 Phase 3: 사용자 앱의 파일 열기도 데이터 드라이브로. */
			finfo = fs_data_search((char *) ebx + ds_base);
			if (finfo != 0) {
				fh = &task->fhandle[i];
				fh->size = finfo->size;
				fh->pos = 0;
				fh->mode = FH_MODE_READ;
				fh->finfo = 0;
				if (fh->size == 0) {
					fh->buf = 0;
					reg[7] = (int) fh;
				} else {
					fh->buf = fs_data_loadfile(finfo->clustno, &fh->size);
					if (fh->buf != 0) {
						reg[7] = (int) fh;
					} else {
						filehandle_close(memman, fh);
					}
				}
			}
		}
	} else if (edx == 22) {
		// api_fclose
		fh = (struct FILEHANDLE *) eax;
		filehandle_close(memman, fh);
	} else if (edx == 23) {
		// api_fseek
		fh = (struct FILEHANDLE *) eax;
		if (fh != 0 && fh->mode != FH_MODE_FREE) {
			if (ecx == 0) {
				fh->pos = ebx;
			} else if (ecx == 1) {
				fh->pos += ebx;
			} else if (ecx == 2) {
				fh->pos = fh->size + ebx;
			}
			if (fh->pos < 0) {
				fh->pos = 0;
			}
			if (fh->pos > fh->size) {
				fh->pos = fh->size;
			}
		}
	} else if (edx == 24) {
		// api_fsize
		fh = (struct FILEHANDLE *) eax;
		reg[7] = 0;
		if (fh != 0 && fh->mode != FH_MODE_FREE) {
			if (ecx == 0) {
				reg[7] = fh->size;
			} else if (ecx == 1) {
				reg[7] = fh->pos;
			} else if (ecx == 2) {
				reg[7] = fh->pos - fh->size;
			}
		}
	} else if (edx == 25) {
		// api_fread
		fh = (struct FILEHANDLE *) eax;
		i = 0;
		if (fh != 0 && fh->mode == FH_MODE_READ) {
			for (i = 0; i < ecx; i++) {
				if (fh->pos == fh->size) {
					break;
				}
				*((char *) ebx + ds_base + i) = fh->buf[fh->pos];
				fh->pos++;
			}
		}
		reg[7] = i;
	} else if (edx == 26) {
		// api_cmdline
		i = 0;
		for (;;) {
			*((char *) ebx + ds_base + i) =  task->cmdline[i];
			if (task->cmdline[i] == 0) {
				break;
			}
			if (i >= ecx) {
				break;
			}
			i++;
		}
		reg[7] = i;
	} else if (edx == 27) {
		// api_getlang
		reg[7] = task->langmode;
	} else if (edx == 28) {
		// api_fopen_w
		for (i = 0; i < 8; i++) {
			if (task->fhandle[i].mode == FH_MODE_FREE) {
				break;
			}
		}
		reg[7] = 0;
		if (i < 8) {
			char *name = (char *) ebx + ds_base;
			finfo = fs_data_search(name);
			if (finfo == 0) {
				if (fs_data_create(name) == 0) {
					finfo = fs_data_search(name);
				}
			} else if (fs_data_truncate(finfo, 0) != 0) {
				finfo = 0;
			}
			if (finfo != 0) {
				fh = &task->fhandle[i];
				fh->buf = 0;
				fh->size = finfo->size;
				fh->pos = 0;
				fh->mode = FH_MODE_WRITE;
				fh->finfo = finfo;
				reg[7] = (int) fh;
			}
		}
	} else if (edx == 29) {
		// api_fwrite
		fh = (struct FILEHANDLE *) eax;
		reg[7] = 0;
		if (fh != 0 && fh->mode == FH_MODE_WRITE && fh->finfo != 0 && ecx >= 0) {
			i = fs_data_write(fh->finfo, fh->pos, (char *) ebx + ds_base, ecx);
			if (i > 0) {
				fh->pos += i;
				fh->size = fh->finfo->size;
			}
			reg[7] = i;
		}
	} else if (edx == 30) {
		// api_fdelete
		finfo = fs_data_search((char *) ebx + ds_base);
		if (finfo != 0 && fs_data_unlink(finfo) == 0) {
			reg[7] = 0;
		} else {
			reg[7] = -1;
		}
	}
	return 0;
}

int *inthandler0c(int *esp)
{
	struct TASK *task = task_now();
	struct CONSOLE *cons = task->cons;
	char s[30];
	cons_putstr0(cons, "\nINT 0C :\n Stack Exception.\n");
	sprintf(s, "EIP = %08X\n", esp[11]);
	cons_putstr0(cons, s);
	return &(task->tss.esp0);	/* 이상종료(ABEND) 시킨다 */
}

int *inthandler0d(int *esp)
{
	struct TASK *task = task_now();
	struct CONSOLE *cons = task->cons;
	char s[30];
	cons_putstr0(cons, "\nINT 0D :\n General Protected Exception.\n");
	sprintf(s, "EIP = %08X\n", esp[11]);
	cons_putstr0(cons, s);
	return &(task->tss.esp0);	/* 이상종료(ABEND) 시킨다 */
}

void hrb_api_linewin(struct SHEET *sht, int x0, int y0, int x1, int y1, int col)
{
	int i, x, y, len, dx, dy;

	dx = x1 - x0;
	dy = y1 - y0;
	x = x0 << 10;
	y = y0 << 10;
	if (dx < 0) {
		dx = - dx;
	}
	if (dy < 0) {
		dy = - dy;
	}
	if (dx >= dy) {
		len = dx + 1;
		if (x0 > x1) {
			dx = -1024;
		} else {
			dx =  1024;
		}
		if (y0 <= y1) {
			dy = ((y1 - y0 + 1) << 10) / len;
		} else {
			dy = ((y1 - y0 - 1) << 10) / len;
		}
	} else {
		len = dy + 1;
		if (y0 > y1) {
			dy = -1024;
		} else {
			dy =  1024;
		}
		if (x0 <= x1) {
			dx = ((x1 - x0 + 1) << 10) / len;
		} else {
			dx = ((x1 - x0 - 1) << 10) / len;
		}
	}

	for (i = 0; i < len; i++) {
		sht->buf[(y >> 10) * sht->bxsize + (x >> 10)] = col;
		x += dx;
		y += dy;
	}

	return;
}







/* ────────────────────────────────────────────────────────────────────
 *  공통 스크롤 텍스트 영역 — console/debug 양쪽에서 사용한다.
 * ──────────────────────────────────────────────────────────────────── */

static int scrollwin_text_w(struct SCROLLWIN *sw) { return sw->wd - SCROLLBAR_W; }
static int scrollwin_visible_lines(struct SCROLLWIN *sw) { return sw->ht / 16; }

int scrollwin_text_cols(struct SCROLLWIN *sw)
{
	int cols = scrollwin_text_w(sw) / 8;
	if (cols > SCROLL_LINE_CHARS) cols = SCROLL_LINE_CHARS;
	return cols;
}

static struct SCROLLLINE *scrollwin_cur_line(struct SCROLLWIN *sw)
{
	int idx = (sw->head + SCROLL_MAX_LINES - 1) % SCROLL_MAX_LINES;
	return &sw->lines[idx];
}

static struct SCROLLLINE *scrollwin_line_at(struct SCROLLWIN *sw, int display_idx)
{
	int oldest = (sw->head - sw->count + SCROLL_MAX_LINES) % SCROLL_MAX_LINES;
	int idx = (oldest + display_idx) % SCROLL_MAX_LINES;
	return &sw->lines[idx];
}

void scrollwin_scroll_to(struct SCROLLWIN *sw, int new_top)
{
	int vis = scrollwin_visible_lines(sw);
	int max_top = (sw->count > vis) ? (sw->count - vis) : 0;
	if (new_top < 0)       new_top = 0;
	if (new_top > max_top) new_top = max_top;
	sw->scroll_top  = new_top;
	sw->auto_follow = (new_top == max_top) ? 1 : 0;
	scrollwin_redraw(sw);
}

static void scrollwin_newline(struct SCROLLWIN *sw)
{
	int idx = sw->head;
	sw->lines[idx].len = 0;
	sw->head = (sw->head + 1) % SCROLL_MAX_LINES;
	if (sw->count < SCROLL_MAX_LINES) {
		sw->count++;
	}
	sw->cx = 0;
	if (sw->auto_follow) {
		int vis = scrollwin_visible_lines(sw);
		sw->scroll_top = (sw->count > vis) ? (sw->count - vis) : 0;
	}
	scrollwin_redraw(sw);
}

void scrollwin_putc(struct SCROLLWIN *sw, int chr, int fc)
{
	struct SCROLLLINE *ln;
	int max_chars = scrollwin_text_cols(sw);
	if (chr == 0x0a) {
		scrollwin_newline(sw);
		return;
	}
	if (chr == 0x0d) {
		return;
	}
	if (chr == 0x09) {
		do {
			scrollwin_putc(sw, ' ', fc);
			ln = scrollwin_cur_line(sw);
		} while ((ln->len & 0x03) != 0);
		return;
	}
	ln = scrollwin_cur_line(sw);
	if (ln->len >= max_chars) {
		scrollwin_newline(sw);
		ln = scrollwin_cur_line(sw);
	}
	ln->text[ln->len] = (unsigned char) chr;
	ln->color[ln->len] = (unsigned char) fc;
	ln->len++;
	sw->cx = ln->len;
	if (sw->auto_follow) {
		int vis = scrollwin_visible_lines(sw);
		sw->scroll_top = (sw->count > vis) ? (sw->count - vis) : 0;
	}
	scrollwin_redraw(sw);
}

void scrollwin_puts(struct SCROLLWIN *sw, char *s, int c)
{
	for (; *s != 0; s++) {
		scrollwin_putc(sw, (unsigned char) *s, c);
	}
}

void scrollwin_backspace(struct SCROLLWIN *sw)
{
	struct SCROLLLINE *ln = scrollwin_cur_line(sw);
	if (ln->len > 0) {
		ln->len--;
		sw->cx = ln->len;
		if (sw->auto_follow) {
			int vis = scrollwin_visible_lines(sw);
			sw->scroll_top = (sw->count > vis) ? (sw->count - vis) : 0;
		}
		scrollwin_redraw(sw);
	}
}

int scrollwin_cursor_x(struct SCROLLWIN *sw)
{
	return sw->x0 + scrollwin_cur_line(sw)->len * 8;
}

int scrollwin_cursor_y(struct SCROLLWIN *sw)
{
	int cur_idx = sw->count - 1;
	int line = cur_idx - sw->scroll_top;
	if (line < 0) line = 0;
	if (line >= scrollwin_visible_lines(sw)) line = scrollwin_visible_lines(sw) - 1;
	return sw->y0 + line * 16;
}

void scrollwin_redraw(struct SCROLLWIN *sw)
{
	extern char hankaku[4096];
	struct SHEET *sht = sw->sht;
	int x, y, line, vis, tw;
	int scrollbar_x, track_h, thumb_h, thumb_y, max_top;
	if (sht == 0) return;
	tw = scrollwin_text_w(sw);
	vis = scrollwin_visible_lines(sw);
	scrollbar_x = sw->x0 + tw;
	for (y = sw->y0; y < sw->y0 + sw->ht; y++) {
		for (x = sw->x0; x < sw->x0 + tw; x++) {
			sht->buf[x + y * sht->bxsize] = sw->bc;
		}
	}
	for (line = 0; line < vis; line++) {
		int idx_disp = sw->scroll_top + line;
		struct SCROLLLINE *ln;
		int yy = sw->y0 + line * 16;
		int j;
		if (idx_disp >= sw->count) break;
		ln = scrollwin_line_at(sw, idx_disp);
		for (j = 0; j < ln->len; j++) {
			putfont8(sht->buf, sht->bxsize, sw->x0 + j * 8, yy,
					ln->color[j], hankaku + ln->text[j] * 16);
		}
	}
	for (y = sw->y0; y < sw->y0 + sw->ht; y++) {
		for (x = scrollbar_x; x < sw->x0 + sw->wd; x++) {
			sht->buf[x + y * sht->bxsize] = COL8_848484;
		}
	}
	max_top = (sw->count > vis) ? (sw->count - vis) : 0;
	if (sw->count <= vis) {
		thumb_h = sw->ht;
		thumb_y = sw->y0;
	} else {
		track_h = sw->ht;
		thumb_h = (track_h * vis) / sw->count;
		if (thumb_h < 8) thumb_h = 8;
		thumb_y = sw->y0 + ((track_h - thumb_h) * sw->scroll_top) / max_top;
	}
	for (y = thumb_y; y < thumb_y + thumb_h && y < sw->y0 + sw->ht; y++) {
		for (x = scrollbar_x + 1; x < sw->x0 + sw->wd - 1; x++) {
			sht->buf[x + y * sht->bxsize] = COL8_FFFFFF;
		}
	}
	sheet_refresh(sht, sw->x0, sw->y0, sw->x0 + sw->wd, sw->y0 + sw->ht);
}

int scrollwin_handle_mouse(struct SCROLLWIN *sw, int mx, int my, int btn)
{
	int sb_x0 = sw->x0 + scrollwin_text_w(sw);
	int sb_x1 = sw->x0 + sw->wd;
	int sb_y0 = sw->y0;
	int sb_y1 = sw->y0 + sw->ht;
	int in_sb = (sb_x0 <= mx && mx < sb_x1 && sb_y0 <= my && my < sb_y1);
	if (sw->sb_grab) {
		if ((btn & 0x01) == 0) {
			sw->sb_grab = 0;
		} else {
			int vis = scrollwin_visible_lines(sw);
			int max_top = (sw->count > vis) ? (sw->count - vis) : 0;
			int track_h = sw->ht;
			int thumb_h = (max_top > 0) ? ((track_h * vis) / sw->count) : track_h;
			int travel = track_h - thumb_h;
			int dy = my - sw->sb_grab_y;
			int new_top = sw->sb_grab_top;
			if (travel > 0 && max_top > 0) {
				new_top = sw->sb_grab_top + (dy * max_top) / travel;
			}
			scrollwin_scroll_to(sw, new_top);
		}
		return 1;
	}
	if (in_sb && (btn & 0x01) != 0) {
		int vis = scrollwin_visible_lines(sw);
		int max_top = (sw->count > vis) ? (sw->count - vis) : 0;
		int track_h = sw->ht;
		int thumb_h = (max_top > 0) ? ((track_h * vis) / sw->count) : track_h;
		int rel = my - sw->y0 - thumb_h / 2;
		int travel = track_h - thumb_h;
		int target = (travel <= 0 || max_top <= 0) ? 0 : (rel * max_top) / travel;
		sw->sb_grab = 1;
		sw->sb_grab_y = my;
		sw->sb_grab_top = sw->scroll_top;
		scrollwin_scroll_to(sw, target);
		sw->sb_grab_top = sw->scroll_top;
		return 1;
	}
	return 0;
}

void scrollwin_init(struct SCROLLWIN *sw, struct SHEET *sht,
		int x0, int y0, int wd, int ht, int bc)
{
	int i;
	sw->sht = sht;
	sw->bc = bc;
	sw->x0 = x0;
	sw->y0 = y0;
	sw->wd = wd & ~7;
	sw->ht = ht & ~15;
	if (sw->wd < 16 + SCROLLBAR_W) sw->wd = 16 + SCROLLBAR_W;
	if (sw->ht < 16) sw->ht = 16;
	sw->cx = 0;
	for (i = 0; i < SCROLL_MAX_LINES; i++) {
		sw->lines[i].len = 0;
	}
	sw->head = 1;
	sw->count = 1;
	sw->scroll_top = 0;
	sw->auto_follow = 1;
	sw->sb_grab = 0;
	sw->sb_grab_y = 0;
	sw->sb_grab_top = 0;
	scrollwin_redraw(sw);
}

struct DBGWIN *dbg_get(void)
{
	return &dbg;
}

void dbg_newline(struct DBGWIN *d)
{
	scrollwin_putc(&d->sw, '\n', COL8_FFFFFF);
}

void dbg_putstr0(char *s, int c)
{
	scrollwin_puts(&dbg.sw, s, c);
}

void dbg_putstr1(char *s, int l, int c)
{
	int i;
	for (i = 0; i < l; i++) {
		scrollwin_putc(&dbg.sw, (unsigned char) s[i], c);
	}
}

void dbg_redraw(struct DBGWIN *d)
{
	scrollwin_redraw(&d->sw);
}

void dbg_scroll_to(struct DBGWIN *d, int new_top)
{
	scrollwin_scroll_to(&d->sw, new_top);
}

int dbg_handle_mouse(struct DBGWIN *d, int mx, int my, int btn)
{
	return scrollwin_handle_mouse(&d->sw, mx, my, btn);
}

void dbg_init(struct SHTCTL *shtctl)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	unsigned char *buf;
	dbg.sht = sheet_alloc(shtctl);
	buf = (unsigned char *) memman_alloc_4k(memman, 416 * 272);
	sheet_setbuf(dbg.sht, buf, 416, 272, -1);
	make_window8(buf, 416, 272, "debug", 0);
	dbg.sht->task = 0;
	dbg.sht->scroll = &dbg.sw;
	dbg.sht->flags |= SHEET_FLAG_SCROLLWIN;
	scrollwin_init(&dbg.sw, dbg.sht, 8, 28, 400, 240,
			16 + 1 + 3 * 6 + 4 * 36);
	make_textbox8(dbg.sht, 8, 28, 400, 240,
			16 + 1 + 3 * 6 + 4 * 36);
	scrollwin_redraw(&dbg.sw);
}
