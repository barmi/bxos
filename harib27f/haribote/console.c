/* 콘솔 관계 */

#include "bootpack.h"
#include "../../tools/modern/euckr_map.h"
#include <stdio.h>
#include <string.h>

struct DBGWIN dbg;

static int cons_text_w(struct CONSOLE *cons);
static int cons_text_h(struct CONSOLE *cons);
static int scrollline_display_cols(struct SCROLLWIN *sw, struct SCROLLLINE *ln);
static void scrollline_draw(struct SCROLLWIN *sw, struct SCROLLLINE *ln, int yy);
static void cons_input_backspace(struct CONSOLE *cons, char *cmdline, int *cmd_len);
static void cons_input_clear(struct CONSOLE *cons, char *cmdline, int *cmd_len);
static void cons_input_set(struct CONSOLE *cons, char *cmdline, int *cmd_len, char *src);
static void cons_history_add(char history[CONS_HISTORY_MAX][CONS_CMDLINE_MAX],
		int *hist_count, char *cmdline);
static char *skip_spaces(char *p);
static int parse_decimal(char *p, int *out);
static int split_two_args(char *args, char **arg1, char **arg2);
static void name83_to_text(unsigned char name83[11], char *dst);
static int normalize_path(char *cwd, char *path, char out[MAX_PATH]);
static int cons_open_file(struct CONSOLE *cons, char *path, struct FS_FILE *file);
static int cons_create_file(struct CONSOLE *cons, char *path, struct FS_FILE *file);
static void filehandle_close(struct MEMMAN *memman, struct FILEHANDLE *fh);
static int copy_file_raw(struct CONSOLE *cons, char *src_name, char *dst_name);
static struct FILEINFO *app_find(struct CONSOLE *cons, char *cmdline, char *app_name);
static void app_name_from_finfo(char *dst, struct FILEINFO *finfo);
static int app_subsystem(struct CONSOLE *cons, char *cmdline, int *fat);
static void task_set_app(struct TASK *task, char *name, int app_type);
static void task_reset_console(struct TASK *task);

#define FH_MODE_FREE	0
#define FH_MODE_READ	1
#define FH_MODE_WRITE	2

/* work4 Phase 2: BX_EVENT 한 개를 task 의 circular event 큐에 enqueue 한다.
 *   - resize drag 는 같은 window 의 pending RESIZE 를 최신 w/h 로 coalesce.
 *     mouse drag 처럼 동일 좌표가 연속으로 들어오는 경우는 호출자가 판단하고
 *     필요하면 producer 측에서 이전 MOUSE_MOVE 와 합쳐 넣어도 된다.
 *   - 큐가 가득 찼으면 새 이벤트를 drop (가장 오래된 클릭 손실 방지).
 *   - fifo32_put(BX_EVENT_FIFO_MARKER) 로 task 를 깨운다.                       */
void bx_event_post(struct TASK *task, int type, int win, int x, int y,
		int button, int key, int w, int h)
{
	int idx, scan;
	struct BX_EVENT *ev;
	if (task == 0 || task->event_buf == 0 || task->event_size <= 0) {
		return;
	}
	if (type == BX_EVENT_RESIZE) {
		for (scan = 0; scan < task->event_count; scan++) {
			idx = (task->event_p + scan) % task->event_size;
			ev = &task->event_buf[idx];
			if (ev->type == BX_EVENT_RESIZE && ev->win == win) {
				ev->w = w;
				ev->h = h;
				return;
			}
		}
	}
	if (task->event_count >= task->event_size) {
		return;	/* drop: 큐 full */
	}
	idx = (task->event_p + task->event_count) % task->event_size;
	ev = &task->event_buf[idx];
	ev->type   = type;
	ev->win    = win;
	ev->x      = x;
	ev->y      = y;
	ev->button = button;
	ev->key    = key;
	ev->w      = w;
	ev->h      = h;
	task->event_count++;
	fifo32_put(&task->fifo, BX_EVENT_FIFO_MARKER);
	return;
}

void console_task(struct SHEET *sheet, int memtotal)
{
	struct TASK *task = task_now();
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	int i, *fat = (int *) memman_alloc_4k(memman, 4 * 2880);
	struct CONSOLE cons;
	struct FILEHANDLE fhandle[8];
	struct DIRHANDLE dhandle[DIR_HANDLES_PER_TASK];
	struct BX_EVENT event_buf[BX_EVENT_QUEUE_LEN];
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
	cons.cwd_clus = task->cwd_clus;
	strcpy(cons.cwd_path, task->cwd_path[0] != 0 ? task->cwd_path : "/");
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
		fhandle[i].file.finfo.name[0] = 0;
	}
	task->fhandle = fhandle;
	for (i = 0; i < DIR_HANDLES_PER_TASK; i++) {
		dhandle[i].in_use = 0;
		dhandle[i].it.at_end = 1;
	}
	task->dhandle = dhandle;
	task->event_buf = event_buf;
	task->event_size = BX_EVENT_QUEUE_LEN;
	task->event_count = 0;
	task->event_p = 0;
	task->fat = fat;
	if (nihongo[4096] != 0xff) {	/* 일본어 폰트 파일을 읽어들일 수 있었는지?  */
		task->langmode = 1;
	} else {
		task->langmode = 0;
	}
	task->langbyte1 = 0;
	task->langbyte2 = 0;

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
	struct TASK *task = task_now();
	char s[2];
	int W = cons_text_w(cons);
	int H = cons_text_h(cons);
	if (cons->scroll != 0) {
		if (move != 0 || chr == 0x0a || chr == 0x09) {
			cons->scroll->langmode = task->langmode;
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
			if (task->langmode == 4 && task->langbyte1 != 0 && task->langbyte2 == 0) {
				/* skip cursor advance for mid byte to make total advance 16px */
			} else {
				/* move가 0일 때는 커서를 진행시키지 않는다 */
				cons->cur_x += 8;
				if (cons->cur_x >= 8 + W) {
					cons_newline(cons);
				}
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
	if (task->langmode >= 1 && task->langmode <= 4 && task->langbyte1 != 0) {
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
	} else if ((strcmp(cmdline, "dir") == 0 || strncmp(cmdline, "dir ", 4) == 0) && cons->sht != 0) {
		cmd_dir(cons, cmdline);
	} else if (strcmp(cmdline, "task") == 0) {
		cmd_task();
	} else if (strcmp(cmdline, "disk") == 0 && cons->sht != 0) {
		cmd_disk(cons);
	} else if (strncmp(cmdline, "resolve ", 8) == 0 && cons->sht != 0) {
		cmd_resolve(cons, cmdline);
	} else if (strncmp(cmdline, "mkdir ", 6) == 0 && cons->sht != 0) {
		cmd_mkdir(cons, cmdline);
	} else if (strncmp(cmdline, "rmdir ", 6) == 0 && cons->sht != 0) {
		cmd_rmdir(cons, cmdline);
	} else if (strcmp(cmdline, "pwd") == 0 && cons->sht != 0) {
		cmd_pwd(cons);
	} else if ((strcmp(cmdline, "cd") == 0 || strncmp(cmdline, "cd ", 3) == 0) && cons->sht != 0) {
		cmd_cd(cons, cmdline);
	} else if (strncmp(cmdline, "touch ", 6) == 0 && cons->sht != 0) {
		cmd_touch(cons, cmdline);
	} else if (strncmp(cmdline, "rm ", 3) == 0 && cons->sht != 0) {
		cmd_rm(cons, cmdline);
	} else if (strncmp(cmdline, "cp ", 3) == 0 && cons->sht != 0) {
		cmd_cp(cons, cmdline);
	} else if (strncmp(cmdline, "mv ", 3) == 0 && cons->sht != 0) {
		cmd_mv(cons, cmdline);
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

static int split_two_args(char *args, char **arg1, char **arg2)
{
	char *p;
	*arg1 = skip_spaces(args);
	if (**arg1 == 0) {
		return -1;
	}
	p = *arg1;
	while (*p > ' ') {
		p++;
	}
	if (*p == 0) {
		return -1;
	}
	*p++ = 0;
	*arg2 = skip_spaces(p);
	if (**arg2 == 0) {
		return -1;
	}
	p = *arg2;
	while (*p > ' ') {
		p++;
	}
	*p = 0;
	return 0;
}

static void name83_to_text(unsigned char name83[11], char *dst)
{
	int i, j = 0, ext = 0;
	if (name83[0] == 0) {
		dst[0] = '(';
		dst[1] = 'n';
		dst[2] = 'o';
		dst[3] = 'n';
		dst[4] = 'e';
		dst[5] = ')';
		dst[6] = 0;
		return;
	}
	for (i = 0; i < 8 && name83[i] != ' '; i++) {
		dst[j++] = name83[i];
	}
	for (i = 8; i < 11; i++) {
		if (name83[i] != ' ') {
			ext = 1;
		}
	}
	if (ext) {
		dst[j++] = '.';
		for (i = 8; i < 11 && name83[i] != ' '; i++) {
			dst[j++] = name83[i];
		}
	}
	dst[j] = 0;
	return;
}

static int append_path_component(char out[MAX_PATH], int *len, char *comp, int clen)
{
	int i;
	if (clen == 0 || (clen == 1 && comp[0] == '.')) {
		return 0;
	}
	if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
		if (*len > 1) {
			(*len)--;
			while (*len > 1 && out[*len - 1] != '/') {
				(*len)--;
			}
			out[*len] = 0;
		}
		return 0;
	}
	if (*len > 1) {
		if (*len + 1 >= MAX_PATH) {
			return -1;
		}
		out[(*len)++] = '/';
	}
	for (i = 0; i < clen; i++) {
		char c = comp[i];
		if ('a' <= c && c <= 'z') {
			c -= 0x20;
		}
		if (*len + 1 >= MAX_PATH) {
			return -1;
		}
		out[(*len)++] = c;
	}
	out[*len] = 0;
	return 0;
}

static int normalize_path(char *cwd, char *path, char out[MAX_PATH])
{
	char joined[MAX_PATH * 2];
	char *p, *q;
	int i = 0, len = 1, clen;

	if (path == 0 || path[0] == 0) {
		return -1;
	}
	if (path[0] == '/') {
		for (i = 0; path[i] != 0 && i < (int) sizeof joined - 1; i++) {
			joined[i] = path[i];
		}
	} else {
		for (i = 0; cwd[i] != 0 && i < (int) sizeof joined - 1; i++) {
			joined[i] = cwd[i];
		}
		if (i > 1 && i < (int) sizeof joined - 1) {
			joined[i++] = '/';
		}
		for (p = path; *p != 0 && i < (int) sizeof joined - 1; p++) {
			joined[i++] = *p;
		}
	}
	if (i >= (int) sizeof joined - 1) {
		return -1;
	}
	joined[i] = 0;
	out[0] = '/';
	out[1] = 0;
	p = joined;
	while (*p != 0) {
		while (*p == '/') {
			p++;
		}
		if (*p == 0) {
			break;
		}
		q = p;
		while (*q != 0 && *q != '/') {
			q++;
		}
		clen = q - p;
		if (append_path_component(out, &len, p, clen) != 0) {
			return -1;
		}
		p = q;
	}
	return 0;
}

static int cons_open_file(struct CONSOLE *cons, char *path, struct FS_FILE *file)
{
	return fs_data_open_path(cons->cwd_clus, path, file);
}

static int cons_create_file(struct CONSOLE *cons, char *path, struct FS_FILE *file)
{
	return fs_data_create_path(cons->cwd_clus, path, file);
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
	fh->file.finfo.name[0] = 0;
	return;
}

static int copy_file_raw(struct CONSOLE *cons, char *src_name, char *dst_name)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct FS_FILE src, dst;
	char *buf = 0;
	int size, r;

	if (cons_open_file(cons, src_name, &src) != 0) {
		return -1;
	}
	r = cons_open_file(cons, dst_name, &dst);
	if (r == 0 && dst.slot.lba == src.slot.lba && dst.slot.offset == src.slot.offset) {
		return -2;
	}
	size = src.finfo.size;
	if (size > 0) {
		buf = (char *) memman_alloc_4k(memman, size);
		if (buf == 0) {
			return -3;
		}
		if (fs_file_read(&src, 0, buf, size) != size) {
			memman_free_4k(memman, (int) buf, size);
			return -4;
		}
	}

	if (r != 0) {
		r = cons_create_file(cons, dst_name, &dst);
		if (r != 0) {
			if (buf != 0) memman_free_4k(memman, (int) buf, size);
			return -5;
		}
	}
	if (fs_file_truncate(&dst, 0) != 0) {
		if (buf != 0) memman_free_4k(memman, (int) buf, size);
		return -6;
	}
	if (size > 0 && fs_file_write(&dst, 0, buf, size) != size) {
		memman_free_4k(memman, (int) buf, size);
		return -7;
	}
	if (buf != 0) {
		memman_free_4k(memman, (int) buf, size);
	}
	return 0;
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
	char *path = skip_spaces(cmdline + 3);
	unsigned int dir_clus = cons->cwd_clus, parent_clus;
	unsigned char leaf_name83[11];
	struct FILEINFO leaf, finfo;
	struct DIR_SLOT slot;
	struct DIR_ITER it;
	int i, j;
	char s[30];
	if (*path != 0) {
		if (fs_resolve_path(cons->cwd_clus, path, &parent_clus,
				leaf_name83, &leaf, &slot) != 0) {
			cons_putstr0(cons, "Directory not found.\n\n");
			return;
		}
		if (leaf_name83[0] == 0) {
			dir_clus = parent_clus;
		} else {
			if (leaf.name[0] == 0 || (leaf.type & 0x10) == 0) {
				cons_putstr0(cons, "Directory not found.\n\n");
				return;
			}
			dir_clus = leaf.clustno;
		}
	}
	if (dir_iter_open(&it, dir_clus) != 0) {
		cons_putstr0(cons, "(no data disk mounted)\n\n");
		return;
	}
	for (;;) {
		i = dir_iter_next(&it, &finfo, &slot);
		if (i <= 0 || finfo.name[0] == 0x00) {
			break;
		}
		if (finfo.name[0] != 0xe5) {
			if ((finfo.type & 0x08) == 0) {
				sprintf(s, "filename.ext   %7d\n", finfo.size);
				for (j = 0; j < 8; j++) {
					s[j] = finfo.name[j];
				}
				s[ 9] = finfo.ext[0];
				s[10] = finfo.ext[1];
				s[11] = finfo.ext[2];
				if ((finfo.type & 0x10) != 0) {
					s[17] = '<';
					s[18] = 'D';
					s[19] = 'I';
					s[20] = 'R';
					s[21] = '>';
				}
				cons_putstr0(cons, s);
				//s[22] = ' ';
				dbg_putstr0(s,COL8_FFFFFF);
			}
		}
	}
	dir_iter_close(&it);
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

void cmd_resolve(struct CONSOLE *cons, char *cmdline)
{
	char *path = skip_spaces(cmdline + 8);
	unsigned int parent_clus;
	unsigned char leaf_name83[11];
	struct FILEINFO leaf;
	struct DIR_SLOT slot;
	char name[16], s[80];
	int r;

	if (*path == 0) {
		cons_putstr0(cons, "usage: resolve <path>\n\n");
		return;
	}
	r = fs_resolve_path(cons->cwd_clus, path, &parent_clus, leaf_name83, &leaf, &slot);
	if (r != 0) {
		sprintf(s, "resolve failed: %d\n\n", r);
		cons_putstr0(cons, s);
		return;
	}
	name83_to_text(leaf_name83, name);
	sprintf(s, "parent=%d leaf=%s\n", parent_clus, name);
	cons_putstr0(cons, s);
	if (leaf.name[0] == 0) {
		cons_putstr0(cons, "found=no\n\n");
		return;
	}
	sprintf(s, "found=yes attr=%02x clus=%d size=%d\n\n",
			leaf.type, leaf.clustno, leaf.size);
	cons_putstr0(cons, s);
	return;
}

void cmd_mkdir(struct CONSOLE *cons, char *cmdline)
{
	char *path = skip_spaces(cmdline + 6);
	int r;

	if (*path == 0) {
		cons_putstr0(cons, "usage: mkdir <path>\n\n");
		return;
	}
	r = fs_mkdir(cons->cwd_clus, path);
	if (r != 0) {
		cons_putstr0(cons, "mkdir failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_rmdir(struct CONSOLE *cons, char *cmdline)
{
	char *path = skip_spaces(cmdline + 6);
	int r;

	if (*path == 0) {
		cons_putstr0(cons, "usage: rmdir <path>\n\n");
		return;
	}
	r = fs_rmdir(cons->cwd_clus, path);
	if (r == FS_RESOLVE_NOT_FOUND) {
		cons_putstr0(cons, "Directory not found.\n\n");
		return;
	}
	if (r == FS_RESOLVE_NOT_DIR) {
		cons_putstr0(cons, "Not a directory.\n\n");
		return;
	}
	if (r == -2) {
		cons_putstr0(cons, "Directory not empty.\n\n");
		return;
	}
	if (r != 0) {
		cons_putstr0(cons, "rmdir failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_pwd(struct CONSOLE *cons)
{
	cons_putstr0(cons, cons->cwd_path);
	cons_putchar(cons, '\n', 1);
	cons_newline(cons);
	return;
}

void cmd_cd(struct CONSOLE *cons, char *cmdline)
{
	struct TASK *task = task_now();
	char *path = skip_spaces(cmdline + 2);
	unsigned int parent_clus, target_clus;
	unsigned char leaf_name83[11];
	struct FILEINFO leaf;
	struct DIR_SLOT slot;
	char new_path[MAX_PATH];
	int r;

	if (*path == 0) {
		path = "/";
	}
	r = fs_resolve_path(cons->cwd_clus, path, &parent_clus,
			leaf_name83, &leaf, &slot);
	if (r != 0) {
		cons_putstr0(cons, "Directory not found.\n\n");
		return;
	}
	if (leaf_name83[0] == 0) {
		target_clus = parent_clus;
	} else {
		if (leaf.name[0] == 0 || (leaf.type & 0x10) == 0) {
			cons_putstr0(cons, "Directory not found.\n\n");
			return;
		}
		target_clus = leaf.clustno;
	}
	if (normalize_path(cons->cwd_path, path, new_path) != 0) {
		cons_putstr0(cons, "Path too long.\n\n");
		return;
	}
	cons->cwd_clus = target_clus;
	strcpy(cons->cwd_path, new_path);
	task->cwd_clus = target_clus;
	strcpy(task->cwd_path, new_path);
	cons_newline(cons);
	return;
}

void cmd_touch(struct CONSOLE *cons, char *cmdline)
{
	char *name = skip_spaces(cmdline + 6);
	struct FS_FILE file;
	int r;

	if (*name == 0) {
		cons_putstr0(cons, "usage: touch <file>\n\n");
		return;
	}
	if (cons_open_file(cons, name, &file) == 0) {
		cons_newline(cons);
		return;
	}
	r = cons_create_file(cons, name, &file);
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
	struct FS_FILE file;

	if (*name == 0) {
		cons_putstr0(cons, "usage: rm <file>\n\n");
		return;
	}
	if (cons_open_file(cons, name, &file) != 0) {
		cons_putstr0(cons, "File not found.\n\n");
		return;
	}
	if (fs_file_unlink(&file) != 0) {
		cons_putstr0(cons, "rm failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_cp(struct CONSOLE *cons, char *cmdline)
{
	char *src, *dst;
	int r;

	if (split_two_args(cmdline + 3, &src, &dst) != 0) {
		cons_putstr0(cons, "usage: cp <src> <dst>\n\n");
		return;
	}
	r = copy_file_raw(cons, src, dst);
	if (r == -1) {
		cons_putstr0(cons, "Source not found.\n\n");
		return;
	}
	if (r != 0) {
		cons_putstr0(cons, "cp failed.\n\n");
		return;
	}
	cons_newline(cons);
	return;
}

void cmd_mv(struct CONSOLE *cons, char *cmdline)
{
	char *src, *dst;
	struct FS_FILE src_file;
	int r;

	if (split_two_args(cmdline + 3, &src, &dst) != 0) {
		cons_putstr0(cons, "usage: mv <src> <dst>\n\n");
		return;
	}
	if (cons_open_file(cons, src, &src_file) != 0) {
		cons_putstr0(cons, "Source not found.\n\n");
		return;
	}
	r = copy_file_raw(cons, src, dst);
	if (r != 0) {
		cons_putstr0(cons, "mv failed.\n\n");
		return;
	}
	if (fs_file_unlink(&src_file) != 0) {
		cons_putstr0(cons, "mv unlink failed.\n\n");
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
	struct FS_FILE file;
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

	r = cons_create_file(cons, name, &file);
	if (r != 0 || fs_file_truncate(&file, 0) != 0 ||
			fs_file_write(&file, 0, data, len) != len) {
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
	struct FS_FILE file;
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

	r = cons_create_file(cons, name, &file);
	if (r != 0 || fs_file_truncate(&file, 0) != 0) {
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
		if (fs_file_write(&file, pos, chunk, n) != n) {
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
	int i, subsystem = app_subsystem(cons, cmdline + 6, fat);
	if (subsystem == HE2_SUBSYSTEM_WINDOW) {
		task = open_constask(0, memtotal);
	} else {
		sht = open_console(shtctl, memtotal);
		task = sht->task;
		sheet_slide(sht, 32, 4);
		sheet_updown(sht, shtctl->top);
		system_request_keywin(sht);
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

void system_start_command(char *cmdline, unsigned int memtotal)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht = 0;
	struct TASK *task;
	struct FIFO32 *fifo;
	int i, subsystem;

	if (cmdline == 0 || cmdline[0] == 0) {
		return;
	}
	subsystem = app_subsystem(0, cmdline, 0);
	if (subsystem == HE2_SUBSYSTEM_WINDOW) {
		task = open_constask(0, memtotal);
	} else {
		sht = open_console(shtctl, memtotal);
		task = sht->task;
		sheet_slide(sht, 32, 4);
		sheet_updown(sht, shtctl->top);
		system_request_keywin(sht);
	}
	fifo = &task->fifo;
	for (i = 0; cmdline[i] != 0 && i < CONS_CMDLINE_MAX - 1; i++) {
		fifo32_put(fifo, cmdline[i] + 256);
	}
	fifo32_put(fifo, 10 + 256);
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
	if (mode <= 4) {
		if ((mode == 3 || mode == 4) && *((int *) 0x0fe0) == 0) {
			cons_putstr0(cons, "hangul font not loaded.\n");
		} else {
			task->langmode = mode;
			task->langbyte1 = 0;
			task->langbyte2 = 0;
			if (cons->scroll != 0) {
				cons->scroll->langmode = mode;
				scrollwin_redraw(cons->scroll);
			}
		}
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

static struct FILEINFO *app_find(struct CONSOLE *cons, char *cmdline, char *app_name)
{
	static struct FS_FILE app_file;
	char name[MAX_PATH];
	int i, has_dot = 0, has_slash = 0;

	for (i = 0; i < MAX_PATH - 5; i++) {
		if (cmdline[i] <= ' ') {
			break;
		}
		if (cmdline[i] == '.') {
			has_dot = 1;
		}
		if (cmdline[i] == '/') {
			has_slash = 1;
		}
		name[i] = cmdline[i];
	}
	name[i] = 0;
	if (i == 0) {
		return 0;
	}

	if (cons != 0 && cons_open_file(cons, name, &app_file) == 0) {
		goto found;
	}
	if (name[0] == '/' && fs_data_open_path(0, name, &app_file) == 0) {
		goto found;
	}
	if (has_slash == 0 && fs_data_open_path(0, name, &app_file) == 0) {
		goto found;
	}
	if (has_dot == 0) {
		name[i    ] = '.';
		name[i + 1] = 'H';
		name[i + 2] = 'E';
		name[i + 3] = '2';
		name[i + 4] = 0;
		if (cons != 0 && cons_open_file(cons, name, &app_file) == 0) {
			goto found;
		}
		if (name[0] == '/' && fs_data_open_path(0, name, &app_file) == 0) {
			goto found;
		}
		if (has_slash == 0 && fs_data_open_path(0, name, &app_file) == 0) {
			goto found;
		}
		name[i + 1] = 'H';
		name[i + 2] = 'R';
		name[i + 3] = 'B';
		if (cons != 0 && cons_open_file(cons, name, &app_file) == 0) {
			goto found;
		}
		if (name[0] == '/' && fs_data_open_path(0, name, &app_file) == 0) {
			goto found;
		}
		if (has_slash == 0 && fs_data_open_path(0, name, &app_file) == 0) {
			goto found;
		}
	}
	return 0;
found:
	if (app_name != 0) {
		app_name_from_finfo(app_name, &app_file.finfo);
	}
	return &app_file.finfo;
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

static int app_subsystem(struct CONSOLE *cons, char *cmdline, int *fat)
{
	struct FILEINFO *finfo;
	char *p;
	int appsiz, subsystem = HE2_SUBSYSTEM_CONSOLE;
	struct he2_hdr_kern *h;

	finfo = app_find(cons, cmdline, 0);
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
	task->cwd_clus = cons->cwd_clus;
	strcpy(task->cwd_path, cons->cwd_path);
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
	if (task->dhandle != 0) {
		for (i = 0; i < DIR_HANDLES_PER_TASK; i++) {
			if (task->dhandle[i].in_use) {
				dir_iter_close(&task->dhandle[i].it);
				task->dhandle[i].in_use = 0;
			}
		}
	}
	if (task->event_buf != 0) {
		task->event_count = 0;
		task->event_p = 0;
	}
	timer_cancelall(&task->fifo);
	memman_free_4k(memman, (int) q, total_sz);
	task->langbyte1 = 0;
	task->langbyte2 = 0;
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
	finfo = app_find(cons, cmdline, app_name);

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
			task->cwd_clus = cons->cwd_clus;
			strcpy(task->cwd_path, cons->cwd_path);
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
			if (task->dhandle != 0) {
				for (i = 0; i < DIR_HANDLES_PER_TASK; i++) {
					if (task->dhandle[i].in_use) {
						dir_iter_close(&task->dhandle[i].it);
						task->dhandle[i].in_use = 0;
					}
				}
			}
			if (task->event_buf != 0) {
				task->event_count = 0;
				task->event_p = 0;
			}
			timer_cancelall(&task->fifo);
			memman_free_4k(memman, (int) q, segsiz);
			task->langbyte1 = 0;
			task->langbyte2 = 0;
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
	struct FS_FILE file;
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
		/* work4 Phase 3: title 을 sheet 에 캐시. api_resizewin 시 frame 재그리기. */
		{
			char *ut = (char *) ecx + ds_base;
			int t;
			for (t = 0; t < (int) sizeof sht->title - 1 && ut[t] != 0; t++) {
				sht->title[t] = ut[t];
			}
			sht->title[t] = 0;
		}
		make_window8((char *) ebx + ds_base, esi, edi, sht->title, 0);
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
			if (fs_data_open_path(task->cwd_clus, (char *) ebx + ds_base, &file) == 0) {
				fh = &task->fhandle[i];
				fh->file = file;
				fh->size = file.finfo.size;
				fh->pos = 0;
				fh->mode = FH_MODE_READ;
				fh->finfo = 0;
				if (fh->size == 0) {
					fh->buf = 0;
					reg[7] = (int) fh;
				} else {
					fh->buf = fs_data_loadfile(file.finfo.clustno, &fh->size);
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
			if (fs_data_create_path(task->cwd_clus, (char *) ebx + ds_base, &file) == 0 &&
					fs_file_truncate(&file, 0) == 0) {
				fh = &task->fhandle[i];
				fh->buf = 0;
				fh->file = file;
				fh->size = file.finfo.size;
				fh->pos = 0;
				fh->mode = FH_MODE_WRITE;
				fh->finfo = &fh->file.finfo;
				reg[7] = (int) fh;
			}
		}
	} else if (edx == 29) {
		// api_fwrite
		fh = (struct FILEHANDLE *) eax;
		reg[7] = 0;
		if (fh != 0 && fh->mode == FH_MODE_WRITE && ecx >= 0) {
			i = fs_file_write(&fh->file, fh->pos, (char *) ebx + ds_base, ecx);
			if (i > 0) {
				fh->pos += i;
				fh->size = fh->file.finfo.size;
				fh->finfo = &fh->file.finfo;
			}
			reg[7] = i;
		}
	} else if (edx == 30) {
		// api_fdelete
		if (fs_data_open_path(task->cwd_clus, (char *) ebx + ds_base, &file) == 0 &&
				fs_file_unlink(&file) == 0) {
			reg[7] = 0;
		} else {
			reg[7] = -1;
		}
	} else if (edx == 31) {
		// api_getcwd
		char *dst = (char *) ebx + ds_base;
		char *src = task->cwd_path;
		reg[7] = -1;
		if (dst != 0 && ecx > 0) {
			for (i = 0; i < ecx - 1 && src[i] != 0; i++) {
				dst[i] = src[i];
			}
			dst[i] = 0;
			reg[7] = i;
		}
	} else if (edx == 32) {
		// api_opendir(path) → handle (DIRHANDLE*) or 0
		struct DIRHANDLE *dh = 0;
		reg[7] = 0;
		if (task->dhandle != 0) {
			for (i = 0; i < DIR_HANDLES_PER_TASK; i++) {
				if (!task->dhandle[i].in_use) {
					dh = &task->dhandle[i];
					break;
				}
			}
			if (dh != 0) {
				if (fs_user_opendir(task->cwd_clus,
						(char *) ebx + ds_base, &dh->it) == 0) {
					dh->in_use = 1;
					reg[7] = (int) dh;
				}
			}
		}
	} else if (edx == 33) {
		// api_readdir(handle, BX_DIRINFO*) → 1=entry,0=end,-1=err
		struct DIRHANDLE *dh = (struct DIRHANDLE *) ebx;
		struct BX_DIRINFO *out = (struct BX_DIRINFO *) ((char *) ecx + ds_base);
		reg[7] = -1;
		if (dh != 0 && task->dhandle != 0 &&
				dh >= &task->dhandle[0] &&
				dh <  &task->dhandle[DIR_HANDLES_PER_TASK] &&
				dh->in_use && ecx != 0) {
			reg[7] = fs_user_readdir(&dh->it, out);
		}
	} else if (edx == 34) {
		// api_closedir(handle)
		struct DIRHANDLE *dh = (struct DIRHANDLE *) ebx;
		if (dh != 0 && task->dhandle != 0 &&
				dh >= &task->dhandle[0] &&
				dh <  &task->dhandle[DIR_HANDLES_PER_TASK] &&
				dh->in_use) {
			dir_iter_close(&dh->it);
			dh->in_use = 0;
		}
	} else if (edx == 35) {
		// api_stat(path, BX_DIRINFO*) → 0/-1
		struct BX_DIRINFO *out = (struct BX_DIRINFO *) ((char *) ecx + ds_base);
		if (ebx == 0 || ecx == 0) {
			reg[7] = -1;
		} else {
			reg[7] = fs_user_stat(task->cwd_clus,
					(char *) ebx + ds_base, out);
		}
	} else if (edx == 36) {
		// api_mkdir(path) → 0/<0
		if (ebx == 0) {
			reg[7] = -1;
		} else {
			reg[7] = fs_mkdir(task->cwd_clus, (char *) ebx + ds_base);
		}
	} else if (edx == 37) {
		// api_rmdir(path) → 0/<0
		if (ebx == 0) {
			reg[7] = -1;
		} else {
			reg[7] = fs_rmdir(task->cwd_clus, (char *) ebx + ds_base);
		}
	} else if (edx == 38) {
		// api_rename(oldpath, newpath) → 0/<0
		if (ebx == 0 || ecx == 0) {
			reg[7] = -1;
		} else {
			reg[7] = fs_user_rename(task->cwd_clus,
					(char *) ebx + ds_base,
					(char *) ecx + ds_base);
		}
	} else if (edx == 40) {
		// api_getevent(BX_EVENT *out, mode) → 1=event,0=none,-1=err
		// mode==0 poll, mode==1 wait. 키보드 char 는 BX_EVENT_KEY 로 변환.
		struct BX_EVENT *out = (struct BX_EVENT *) ((char *) ebx + ds_base);
		int v;
		reg[7] = -1;
		if (ebx == 0 || task->event_buf == 0) {
			return 0;
		}
		for (;;) {
			io_cli();
			/* (a) event 큐가 차 있으면 먼저 꺼낸다 */
			if (task->event_count > 0) {
				*out = task->event_buf[task->event_p];
				task->event_p = (task->event_p + 1) % task->event_size;
				task->event_count--;
				io_sti();
				reg[7] = 1;
				return 0;
			}
			/* (b) fifo 에서 키 / 마커 / 시스템 신호 처리 */
			if (fifo32_status(&task->fifo) == 0) {
				if (ecx != 0) {
					task_sleep(task);
					/* loop and retry */
				} else {
					io_sti();
					reg[7] = 0;
					return 0;
				}
			} else {
				v = fifo32_get(&task->fifo);
				io_sti();
				if (v == BX_EVENT_FIFO_MARKER) {
					/* event 큐에 들어와 있을 것 — 다음 iteration 에 꺼낸다 */
					continue;
				}
				if (v <= 1 && cons->sht != 0) {
					/* 커서용 타이머 — 무시 (다음 콜백 재무장) */
					timer_init(cons->timer, &task->fifo, 1);
					timer_settime(cons->timer, 50);
					continue;
				}
				if (v == 2 || v == 3) {
					/* 커서 ON/OFF — app 에는 의미 없음 */
					continue;
				}
				if (v == 4) {
					/* 콘솔만을 닫음 — api_getkey 와 동일 처리 */
					timer_cancel(cons->timer);
					io_cli();
					fifo32_put(sys_fifo,
							cons->sht - shtctl->sheets0 + 2024);
					cons->sht = 0;
					io_sti();
					continue;
				}
				if (v >= 256 && v < 256 + 0x200) {
					/* 키보드 char → BX_EVENT_KEY */
					out->type = BX_EVENT_KEY;
					out->win = 0;
					out->x = 0; out->y = 0;
					out->button = 0;
					out->key = v - 256;
					out->w = 0; out->h = 0;
					reg[7] = 1;
					return 0;
				}
				/* 알 수 없는 값 — drop and continue */
			}
		}
	} else if (edx == 41) {
		// api_resizewin(win, new_buf, new_w, new_h, col_inv) → 0/-1
		// 앱이 새 buffer 를 alloc 해서 넘기고, 커널이 frame(make_window8)을
		// 새 buffer 에 다시 그린 뒤 sheet_resize 로 교체한다. 앱은 이후
		// client area 만 redraw 하면 된다. 이전 buf 의 free 는 앱 책임.
		struct SHEET *rsht = (struct SHEET *) (ebx & 0xfffffffe);
		char *new_buf = (char *) ecx + ds_base;
		reg[7] = -1;
		if (rsht != 0 && (rsht->flags & SHEET_FLAG_USE) != 0 &&
				rsht->task == task && esi > 0 && edi > 0) {
			/* App window (frame=make_window8) 이면 새 buffer 에도 frame 을
			 * 다시 그려준다. 그 외 종류(콘솔/디버그)는 호출자 자신이 frame 처리. */
			if ((rsht->flags & SHEET_FLAG_APP_WIN) != 0) {
				make_window8((unsigned char *) new_buf, esi, edi,
						rsht->title,
						(shtctl->top >= 0 &&
								shtctl->sheets[shtctl->top] == rsht) ? 1 : 0);
			}
			sheet_resize(rsht, (unsigned char *) new_buf, esi, edi);
			rsht->col_inv = eax;
			reg[7] = 0;
		}
	} else if (edx == 42) {
		// api_set_winevent(win, flags) → 0/-1.
		//   bit0(BX_WIN_EV_MOUSE)  → SHEET_FLAG_APP_EVENTS
		//   bit1(BX_WIN_EV_RESIZE) → SHEET_FLAG_RESIZABLE
		//   bit2(BX_WIN_EV_DBLCLK) → SHEET_FLAG_APP_DBLCLK
		struct SHEET *rsht = (struct SHEET *) (ebx & 0xfffffffe);
		reg[7] = -1;
		if (rsht != 0 && (rsht->flags & SHEET_FLAG_USE) != 0 &&
				rsht->task == task) {
			rsht->flags &= ~(SHEET_FLAG_APP_EVENTS | SHEET_FLAG_RESIZABLE
					| SHEET_FLAG_APP_DBLCLK);
			if (ecx & BX_WIN_EV_MOUSE) {
				rsht->flags |= SHEET_FLAG_APP_EVENTS;
			}
			if (ecx & BX_WIN_EV_RESIZE) {
				rsht->flags |= SHEET_FLAG_RESIZABLE;
			}
			if (ecx & BX_WIN_EV_DBLCLK) {
				rsht->flags |= SHEET_FLAG_APP_DBLCLK;
			}
			reg[7] = 0;
		}
	} else if (edx == 43) {
		// api_setcursor(shape) → 0/-1. shape = BX_CURSOR_*.
		reg[7] = -1;
		if (ebx >= BX_CURSOR_ARROW && ebx < MAX_MOUSE_CURSOR) {
			mouse_set_cursor_shape(ebx);
			reg[7] = 0;
		}
	} else if (edx == 39) {
		// api_exec(path, flags) → 0=launched, <0=error
		// 실행 파일의 부모 디렉터리를 cwd 로 하는 새 console task 를
		// spawn 하고 파일명만 cmdline 으로 주입한다. HE2 subsystem 이
		// CONSOLE 이면 출력 가시화를 위해 콘솔 윈도우를 띄우고,
		// WINDOW 이면 sheet 없는 headless task 로 둔다.
		char *path = (char *) ebx + ds_base;
		struct FS_FILE efile;
		unsigned int parent_clus;
		unsigned char leaf_name83[11];
		struct FILEINFO leaf;
		struct DIR_SLOT slot;
		struct TASK *etask;
		struct FIFO32 *efifo;
		char norm[MAX_PATH], parent_path[MAX_PATH], exec_name[MAX_PATH];
		char *slash;
		char *fbuf;
		int fsize;
		int subsystem;
		struct SHTCTL *exec_shtctl;
		struct SHEET *exec_sht;
		int i, j;
		reg[7] = -1;
		if (ebx != 0 &&
				fs_data_open_path(task->cwd_clus, path, &efile) == 0 &&
				fs_resolve_path(task->cwd_clus, path, &parent_clus,
					leaf_name83, &leaf, &slot) == 0 &&
				leaf.name[0] != 0 &&
				normalize_path(task->cwd_path[0] != 0 ? task->cwd_path : "/",
					path, norm) == 0) {
			/* HE2 헤더의 subsystem flag 만 읽기 위해 임시로 파일을 로드한다.
			 * 새 task 의 cmd_app 이 다시 로드하므로 부담은 한 번 추가될 뿐이다.
			 * 매직이 다르거나 로드 실패 시 console subsystem 으로 가정한다. */
			subsystem = HE2_SUBSYSTEM_CONSOLE;
			fsize = leaf.size;
			if (fsize >= 32) {
				fbuf = fs_data_loadfile(leaf.clustno, &fsize);
				if (fbuf != 0) {
					if (fsize >= 32 && fbuf[0] == 'H' && fbuf[1] == 'E' &&
							fbuf[2] == '2' && fbuf[3] == 0) {
						struct he2_hdr_kern *eh = (struct he2_hdr_kern *) fbuf;
						subsystem = eh->flags & HE2_FLAG_SUBSYSTEM_MASK;
					}
					memman_free_4k((struct MEMMAN *) MEMMAN_ADDR,
						(int) fbuf, fsize);
				}
			}
			slash = norm;
			for (i = 0; norm[i] != 0; i++) {
				if (norm[i] == '/') {
					slash = norm + i;
				}
			}
			if (slash == norm) {
				strcpy(parent_path, "/");
			} else {
				for (i = 0; norm + i < slash && i < MAX_PATH - 1; i++) {
					parent_path[i] = norm[i];
				}
				parent_path[i] = 0;
			}
			strcpy(exec_name, slash + 1);
			if (subsystem == HE2_SUBSYSTEM_WINDOW) {
				etask = open_constask(0, g_memtotal);
			} else {
				exec_shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
				exec_sht = open_console(exec_shtctl, g_memtotal);
				if (exec_sht != 0) {
					etask = exec_sht->task;
					sheet_slide(exec_sht, 32, 4);
					sheet_updown(exec_sht, exec_shtctl->top);
				} else {
					etask = 0;
				}
			}
			if (etask != 0) {
				etask->cwd_clus = parent_clus;
				strcpy(etask->cwd_path, parent_path);
				efifo = &etask->fifo;
				for (j = 0; exec_name[j] != 0; j++) {
					fifo32_put(efifo, exec_name[j] + 256);
				}
				fifo32_put(efifo, 10 + 256);   /* Enter */
				reg[7] = 0;
			}
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

static void draw_wide_hangul(struct SHEET *sht, int x, int y, int c, int codepoint)
{
	char *hangul = (char *) *((int *) 0x0fe0);
	char *font;
	if (hangul == 0 || codepoint < 0xac00 || codepoint > 0xd7a3) {
		return;
	}
	font = hangul + 4096 + (codepoint - 0xac00) * 32;
	putfont8(sht->buf, sht->bxsize, x,     y, c, font);
	putfont8(sht->buf, sht->bxsize, x + 8, y, c, font + 16);
}

static int scrollline_display_cols(struct SCROLLWIN *sw, struct SCROLLLINE *ln)
{
	int j = 0, cols = 0;
	while (j < ln->len) {
		int b = ln->text[j];
		if (sw->langmode == 3 &&
				0xa1 <= b && b <= 0xfe && j + 1 < ln->len) {
			int trail = ln->text[j + 1];
			if (0xa1 <= trail && trail <= 0xfe &&
					g_euckr_to_uhs[(b - 0xa1) * 94 + (trail - 0xa1)] != 0xffff) {
				cols += 2;
				j += 2;
				continue;
			}
		}
		if (sw->langmode == 3 && 0xa1 <= b && b <= 0xfe) {
			break;
		}
		if (sw->langmode == 4 &&
				0xea <= b && b <= 0xed && j + 2 < ln->len) {
			int b2 = ln->text[j + 1], b3 = ln->text[j + 2];
			int cp;
			if (0x80 <= b2 && b2 <= 0xbf && 0x80 <= b3 && b3 <= 0xbf) {
				cp = ((b & 0x0f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
				if (0xac00 <= cp && cp <= 0xd7a3) {
					cols += 2;
					j += 3;
					continue;
				}
			}
		}
		if (sw->langmode == 4 && 0xea <= b && b <= 0xed) {
			break;
		}
		cols++;
		j++;
	}
	return cols;
}

static void scrollline_draw(struct SCROLLWIN *sw, struct SCROLLLINE *ln, int yy)
{
	extern char hankaku[4096];
	struct SHEET *sht = sw->sht;
	int j = 0, col = 0, max_cols = scrollwin_text_cols(sw);
	while (j < ln->len && col < max_cols) {
		int b = ln->text[j];
		int c = ln->color[j];
		if (sw->langmode == 3 &&
				0xa1 <= b && b <= 0xfe && j + 1 < ln->len && col + 1 < max_cols) {
			int trail = ln->text[j + 1];
			if (0xa1 <= trail && trail <= 0xfe) {
				int cp = g_euckr_to_uhs[(b - 0xa1) * 94 + (trail - 0xa1)];
				if (cp != 0xffff) {
					draw_wide_hangul(sht, sw->x0 + col * 8, yy, c, cp);
					col += 2;
					j += 2;
					continue;
				}
			}
		}
		if (sw->langmode == 4 &&
				0xea <= b && b <= 0xed && j + 2 < ln->len && col + 1 < max_cols) {
			int b2 = ln->text[j + 1], b3 = ln->text[j + 2];
			int cp;
			if (0x80 <= b2 && b2 <= 0xbf && 0x80 <= b3 && b3 <= 0xbf) {
				cp = ((b & 0x0f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
				if (0xac00 <= cp && cp <= 0xd7a3) {
					draw_wide_hangul(sht, sw->x0 + col * 8, yy, c, cp);
					col += 2;
					j += 3;
					continue;
				}
			}
		}
		putfont8(sht->buf, sht->bxsize, sw->x0 + col * 8, yy,
				c, hankaku + b * 16);
		col++;
		j++;
	}
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
	struct TASK *task = task_now();
	if (task != 0) {
		sw->langmode = task->langmode;
	}
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
		} while ((scrollline_display_cols(sw, ln) & 0x03) != 0);
		return;
	}
	ln = scrollwin_cur_line(sw);
	if (scrollline_display_cols(sw, ln) >= max_chars ||
			ln->len >= SCROLL_LINE_CHARS) {
		scrollwin_newline(sw);
		ln = scrollwin_cur_line(sw);
	}
	ln->text[ln->len] = (unsigned char) chr;
	ln->color[ln->len] = (unsigned char) fc;
	ln->len++;
	sw->cx = scrollline_display_cols(sw, ln);
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
	return sw->x0 + sw->cx * 8;
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
		if (idx_disp >= sw->count) break;
		ln = scrollwin_line_at(sw, idx_disp);
		scrollline_draw(sw, ln, yy);
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
	sw->langmode = 0;
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
