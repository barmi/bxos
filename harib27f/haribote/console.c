/* 콘솔 관계 */

#include "bootpack.h"
#include <stdio.h>
#include <string.h>

struct DBGWIN dbg;

static int cons_text_w(struct CONSOLE *cons);
static int cons_text_h(struct CONSOLE *cons);

void console_task(struct SHEET *sheet, int memtotal)
{
	struct TASK *task = task_now();
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	int i, *fat = (int *) memman_alloc_4k(memman, 4 * 2880);
	struct CONSOLE cons;
	struct FILEHANDLE fhandle[8];
	char cmdline[30];
	unsigned char *nihongo = (char *) *((int *) 0x0fe8);

	cons.sht = sheet;
	cons.cur_x =  8;
	cons.cur_y = 28;
	cons.cur_c = -1;
	/* 초기 텍스트 영역 크기 — 기본 콘솔 윈도우(256×165) 의 textbox 와 일치.
	 * console_resize() 가 호출되면 이 값이 갱신되어 wrap/scroll 이 따라감.   */
	cons.width  = 240;
	cons.height = 128;
	task->cons = &cons;
	task->cmdline = cmdline;

	if (cons.sht != 0) {
		cons.timer = timer_alloc();
		timer_init(cons.timer, &task->fifo, 1);
		timer_settime(cons.timer, 50);
	}
	file_readfat(fat, (unsigned char *) (ADR_DISKIMG + 0x000200));
	for (i = 0; i < 8; i++) {
		fhandle[i].buf = 0;	/* 미사용 마크 */
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
					if (cons.cur_x > 16) {
						/* 스페이스로 지우고 나서 커서를 1개 back */
						cons_putchar(&cons, ' ', 0);
						cons.cur_x -= 8;
					}
				} else if (i == 10 + 256) {
					/* Enter */
					/* 스페이스로 지우고 나서 개행한다 */
					cons_putchar(&cons, ' ', 0);
					cmdline[cons.cur_x / 8 - 2] = 0;
					cons_newline(&cons);
					cons_runcmd(cmdline, &cons, fat, memtotal);	/* 커맨드 실행 */
					if (cons.sht == 0) {
						cmd_exit(&cons, fat);
					}
					/* prompt 표시 */
					cons_putchar(&cons, '>', 1);
				} else {
					/* 일반 문자 */
					if (cons.cur_x < 8 + cons_text_w(&cons) - 8 &&
							cons.cur_x / 8 - 2 < (int) sizeof(cmdline) - 1) {
						/* 한 글자 표시하고 나서, 커서를 1개 진행한다 */
						cmdline[cons.cur_x / 8 - 2] = i - 256;
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
	if (w < 16 || w > 4096) {
		return 240;
	}
	w &= ~7;	/* glyph width = 8px */
	return (w >= 16) ? w : 16;
}
static int cons_text_h(struct CONSOLE *cons)
{
	int h = cons->height;
	if (h < 16 || h > 4096) {
		return 128;
	}
	h &= ~15;	/* line height = 16px */
	return (h >= 16) ? h : 16;
}

void cons_putchar(struct CONSOLE *cons, int chr, char move)
{
	char s[2];
	int W = cons_text_w(cons);
	int H = cons_text_h(cons);
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
	} else if (strcmp(cmdline, "exit") == 0) {
		cmd_exit(cons, fat);
	} else if (strncmp(cmdline, "start ", 6) == 0) {
		cmd_start(cons, cmdline, memtotal);
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
	struct FILEINFO *finfo = (struct FILEINFO *) (ADR_DISKIMG + 0x002600);
	int i, j;
	char s[30];
	for (i = 0; i < 224; i++) {
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
	dbg_putstr0("ID  NAME            LV  TIME      \n", COL8_FFFFFF);
	dbg_putstr0("--- --------------- --- ----------\n", COL8_FFFFFF);
	for (i = 0; i < taskctl->alloc; i++) {
		if (taskctl->tasks0[i].flags == 0) {
			continue;
		}
		memset(msg, 0, sizeof(msg));
		sprintf(msg, "%3d %-15s   %1d %7d.%02d\n", i,
			taskctl->tasks0[i].name, taskctl->tasks0[i].level,
			taskctl->tasks0[i].time / 100, taskctl->tasks0[i].time % 100);
		dbg_putstr0(msg, COL8_FFFFFF);
	}
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

void cmd_start(struct CONSOLE *cons, char *cmdline, int memtotal)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht = open_console(shtctl, memtotal);
	struct FIFO32 *fifo = &sht->task->fifo;
	int i;
	sheet_slide(sht, 32, 4);
	sheet_updown(sht, shtctl->top);
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

static int load_and_run_he2(struct CONSOLE *cons, struct TASK *task,
		struct MEMMAN *memman, char *p, int appsiz)
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

	strcpy(task->name, "console");
	shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	for (i = 0; i < MAX_SHEETS; i++) {
		sht = &(shtctl->sheets0[i]);
		if ((sht->flags & 0x11) == 0x11 && sht->task == task) {
			sheet_free(sht);
		}
	}
	for (i = 0; i < 8; i++) {
		if (task->fhandle[i].buf != 0) {
			memman_free_4k(memman, (int) task->fhandle[i].buf,
					task->fhandle[i].size);
			task->fhandle[i].buf = 0;
		}
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
	char name[18], *p, *q;
	struct TASK *task = task_now();
	int i, segsiz, datsiz, esp, dathrb, appsiz;
	struct SHTCTL *shtctl;
	struct SHEET *sht;

	/* 커맨드 라인으로부터 파일명을 생성 */
	for (i = 0; i < 13; i++) {
		if (cmdline[i] <= ' ') {
			break;
		}
		name[i] = cmdline[i];
	}
	name[i] = 0; /* 우선 파일명의 뒤를 0으로 한다 */

	/* 파일을 찾는다 */
	finfo = file_search(name, (struct FILEINFO *) (ADR_DISKIMG + 0x002600), 224);
	if (finfo == 0 && name[i - 1] != '.') {
		/* 새 포맷(.HE2) 우선, 없으면 기존 .HRB 도 시도 */
		name[i    ] = '.';
		name[i + 1] = 'H';
		name[i + 2] = 'E';
		name[i + 3] = '2';
		name[i + 4] = 0;
		finfo = file_search(name, (struct FILEINFO *) (ADR_DISKIMG + 0x002600), 224);
		if (finfo == 0) {
			name[i + 1] = 'H';
			name[i + 2] = 'R';
			name[i + 3] = 'B';
			finfo = file_search(name, (struct FILEINFO *) (ADR_DISKIMG + 0x002600), 224);
		}
	}

	if (finfo != 0) {
		/* 파일이 발견되었을 경우 */
		appsiz = finfo->size;
		p = file_loadfile2(finfo->clustno, &appsiz, fat);

		/* HE2 인지 먼저 검사 (magic = "HE2\0") */
		if (appsiz >= 32 && p[0] == 'H' && p[1] == 'E' && p[2] == '2' && p[3] == 0) {
			load_and_run_he2(cons, task, memman, p, appsiz);
		} else if (appsiz >= 36 && strncmp(p + 4, "Hari", 4) == 0 && *p == 0x00) {
			segsiz = *((int *) (p + 0x0000));
			esp    = *((int *) (p + 0x000c));
			datsiz = *((int *) (p + 0x0010));
			dathrb = *((int *) (p + 0x0014));
			q = (char *) memman_alloc_4k(memman, segsiz);
			task->ds_base = (int) q;
			set_segmdesc(task->ldt + 0, appsiz - 1, (int) p, AR_CODE32_ER + 0x60);
			set_segmdesc(task->ldt + 1, segsiz - 1, (int) q, AR_DATA32_RW + 0x60);
			for (i = 0; i < datsiz; i++) {
				q[esp + i] = p[dathrb + i];
			}
			start_app(0x1b, 0 * 8 + 4, esp, 1 * 8 + 4, &(task->tss.esp0));
			strcpy(task->name, "console");
			shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
			for (i = 0; i < MAX_SHEETS; i++) {
				sht = &(shtctl->sheets0[i]);
				if ((sht->flags & 0x11) == 0x11 && sht->task == task) {
					/* 어플리케이션 open & 레이어 발견 */
					sheet_free(sht);	/* 닫는다 */
				}
			}
			for (i = 0; i < 8; i++) {	/* 클로우즈 하지 않는 파일을 클로우즈 */
				if (task->fhandle[i].buf != 0) {
					memman_free_4k(memman, (int) task->fhandle[i].buf, task->fhandle[i].size);
					task->fhandle[i].buf = 0;
				}
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
			if (task->fhandle[i].buf == 0) {
				break;
			}
		}
		fh = &task->fhandle[i];
		reg[7] = 0;
		if (i < 8) {
			finfo = file_search((char *) ebx + ds_base,
					(struct FILEINFO *) (ADR_DISKIMG + 0x002600), 224);
			if (finfo != 0) {
				reg[7] = (int) fh;
				fh->size = finfo->size;
				fh->pos = 0;
				fh->buf = file_loadfile2(finfo->clustno, &fh->size, task->fat);
			}
		}
	} else if (edx == 22) {
		// api_fclose
		fh = (struct FILEHANDLE *) eax;
		memman_free_4k(memman, (int) fh->buf, fh->size);
		fh->buf = 0;
	} else if (edx == 23) {
		// api_fseek
		fh = (struct FILEHANDLE *) eax;
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
	} else if (edx == 24) {
		// api_fsize
		fh = (struct FILEHANDLE *) eax;
		if (ecx == 0) {
			reg[7] = fh->size;
		} else if (ecx == 1) {
			reg[7] = fh->pos;
		} else if (ecx == 2) {
			reg[7] = fh->pos - fh->size;
		}
	} else if (edx == 25) {
		// api_fread
		fh = (struct FILEHANDLE *) eax;
		for (i = 0; i < ecx; i++) {
			if (fh->pos == fh->size) {
				break;
			}
			*((char *) ebx + ds_base + i) = fh->buf[fh->pos];
			fh->pos++;
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
 *  디버그 윈도우 — ring buffer 기반 스크롤백 + 우측 스크롤바
 *
 *  레이아웃 (sht_back 위에 직접 그림):
 *
 *    (x0, y0) ┌──────────────────────────────┬──┐
 *             │                              │░░│
 *             │   text area                  │██│ ← scrollbar
 *             │   (wd - SCROLLBAR_W) × ht    │░░│
 *             │                              │░░│
 *             └──────────────────────────────┴──┘
 *
 *  text area : 한 라인 16px, 한 글자 8px. visible_lines = ht / 16.
 *  scrollbar : 우측 DBG_SCROLLBAR_W (10px). thumb 높이는 (visible/total).
 * ──────────────────────────────────────────────────────────────────── */

struct DBGWIN *dbg_get(void)
{
	return &dbg;
}

static int dbg_text_w(struct DBGWIN *d) { return d->wd - DBG_SCROLLBAR_W; }
static int dbg_visible_lines(struct DBGWIN *d) { return d->ht / 16; }

/* head/count 인변형:
 *   - head = "다음 라인을 만들 자리" (mod N)
 *   - count = 보관된 라인 수 (현재 작성중 라인 포함)
 *   - 현재 작성중 라인 = lines[(head - 1 + N) % N]
 *
 * dbg_init 에서 빈 라인 1개를 미리 할당해두므로 (head=1, count=1),
 * 이 함수는 그냥 head-1 인덱스를 돌려주면 된다.                        */
static struct DBGLINE *dbg_cur_line(struct DBGWIN *d)
{
	int idx = (d->head + DBG_MAX_LINES - 1) % DBG_MAX_LINES;
	return &d->lines[idx];
}

/* 새 라인을 ring 에 추가 (개행 시 호출). 자동 follow 모드면 끝으로 스크롤. */
void dbg_newline(struct DBGWIN *d)
{
	int idx = d->head;
	d->lines[idx].len = 0;
	d->head = (d->head + 1) % DBG_MAX_LINES;
	if (d->count < DBG_MAX_LINES) {
		d->count++;
	}
	d->cx = 0;
	if (d->auto_follow) {
		int vis = dbg_visible_lines(d);
		d->scroll_top = (d->count > vis) ? (d->count - vis) : 0;
	}
	dbg_redraw(d);
}

/* 한 글자 추가 — ring buffer 에 저장하고 즉시 redraw.
 *   - 탭은 다음 8픽셀 경계까지 공백.
 *   - 개행/복귀 처리.                                                  */
void dbg_putchar(int chr, int fc)
{
	struct DBGLINE *ln;
	int max_chars = dbg_text_w(&dbg) / 8;
	if (max_chars > DBG_LINE_CHARS) max_chars = DBG_LINE_CHARS;

	if (chr == 0x0a) {	/* '\n' */
		dbg_newline(&dbg);
		return;
	}
	if (chr == 0x0d) {	/* '\r' — ignore */
		return;
	}
	if (chr == 0x09) {	/* tab → space-padding to next 4-char boundary */
		do {
			dbg_putchar(' ', fc);
			ln = dbg_cur_line(&dbg);
		} while ((ln->len & 0x03) != 0);
		return;
	}

	ln = dbg_cur_line(&dbg);
	if (ln->len >= max_chars) {
		dbg_newline(&dbg);
		ln = dbg_cur_line(&dbg);
	}
	ln->text[ln->len]  = (unsigned char) chr;
	ln->color[ln->len] = (unsigned char) fc;
	ln->len++;
	if (dbg.auto_follow) {
		int vis = dbg_visible_lines(&dbg);
		dbg.scroll_top = (dbg.count > vis) ? (dbg.count - vis) : 0;
	}
	dbg_redraw(&dbg);
}

void dbg_putstr0(char *s, int c)
{
	for (; *s != 0; s++) {
		dbg_putchar((unsigned char) *s, c);
	}
}

void dbg_putstr1(char *s, int l, int c)
{
	int i;
	for (i = 0; i < l; i++) {
		dbg_putchar((unsigned char) s[i], c);
	}
}

/* scroll_top 을 [0, max] 사이로 클램프하고 redraw. */
void dbg_scroll_to(struct DBGWIN *d, int new_top)
{
	int vis = dbg_visible_lines(d);
	int max_top = (d->count > vis) ? (d->count - vis) : 0;
	if (new_top < 0)         new_top = 0;
	if (new_top > max_top)   new_top = max_top;
	d->scroll_top    = new_top;
	d->auto_follow   = (new_top == max_top) ? 1 : 0;
	dbg_redraw(d);
}

/* 한 라인의 ring 인덱스 → 표시순서(0=가장 오래됨, count-1=최신)
 *   가장 오래된 라인의 ring 인덱스 = (head - count + N) mod N           */
static struct DBGLINE *dbg_line_at(struct DBGWIN *d, int display_idx)
{
	int oldest = (d->head - d->count + DBG_MAX_LINES) % DBG_MAX_LINES;
	int idx = (oldest + display_idx) % DBG_MAX_LINES;
	return &d->lines[idx];
}

void dbg_redraw(struct DBGWIN *d)
{
	extern char hankaku[4096];
	struct SHEET *sht = d->sht;
	int x, y, line, vis, tw;
	int scrollbar_x;
	int track_h, thumb_h, thumb_y;
	int max_top;

	if (sht == 0) return;

	tw = dbg_text_w(d);
	vis = dbg_visible_lines(d);
	scrollbar_x = d->x0 + tw;

	/* 1) 텍스트 영역 배경 클리어 */
	for (y = d->y0; y < d->y0 + d->ht; y++) {
		for (x = d->x0; x < d->x0 + tw; x++) {
			sht->buf[x + y * sht->bxsize] = d->bc;
		}
	}

	/* 2) 보이는 라인 그리기 */
	for (line = 0; line < vis; line++) {
		int idx_disp = d->scroll_top + line;
		struct DBGLINE *ln;
		int yy = d->y0 + line * 16;
		int j;
		if (idx_disp >= d->count) break;
		ln = dbg_line_at(d, idx_disp);
		for (j = 0; j < ln->len; j++) {
			putfont8(sht->buf, sht->bxsize, d->x0 + j * 8, yy,
					ln->color[j], hankaku + ln->text[j] * 16);
		}
	}

	/* 3) 스크롤바 트랙 (어두운 회색) */
	for (y = d->y0; y < d->y0 + d->ht; y++) {
		for (x = scrollbar_x; x < d->x0 + d->wd; x++) {
			sht->buf[x + y * sht->bxsize] = COL8_848484;
		}
	}

	/* 4) thumb (밝은 회색). 위치/크기 = 비율 기반 */
	max_top = (d->count > vis) ? (d->count - vis) : 0;
	if (d->count <= vis) {
		/* 전부 보임 — 트랙 전체를 thumb 으로 */
		thumb_h = d->ht;
		thumb_y = d->y0;
	} else {
		track_h = d->ht;
		thumb_h = (track_h * vis) / d->count;
		if (thumb_h < 8) thumb_h = 8;
		if (max_top == 0) {
			thumb_y = d->y0;
		} else {
			thumb_y = d->y0 + ((track_h - thumb_h) * d->scroll_top) / max_top;
		}
	}
	for (y = thumb_y; y < thumb_y + thumb_h && y < d->y0 + d->ht; y++) {
		for (x = scrollbar_x + 1; x < d->x0 + d->wd - 1; x++) {
			sht->buf[x + y * sht->bxsize] = COL8_FFFFFF;
		}
	}

	sheet_refresh(sht, d->x0, d->y0, d->x0 + d->wd, d->y0 + d->ht);
}

/* bootpack.c 마우스 루프에서 호출.
 *   반환 1 = dbg 가 이벤트를 소비했음 (다른 sheet 처리 skip)             */
int dbg_handle_mouse(struct DBGWIN *d, int mx, int my, int btn)
{
	int sb_x0 = d->x0 + dbg_text_w(d);
	int sb_x1 = d->x0 + d->wd;
	int sb_y0 = d->y0;
	int sb_y1 = d->y0 + d->ht;
	int in_sb = (sb_x0 <= mx && mx < sb_x1 && sb_y0 <= my && my < sb_y1);

	if (d->sb_grab) {
		if ((btn & 0x01) == 0) {
			/* release */
			d->sb_grab = 0;
		} else {
			/* drag — 마우스 이동량을 scroll_top 변화량으로 환산 */
			int vis = dbg_visible_lines(d);
			int max_top = (d->count > vis) ? (d->count - vis) : 0;
			int track_h = d->ht;
			int thumb_h = (max_top > 0) ? ((track_h * vis) / d->count) : track_h;
			int travel  = track_h - thumb_h;
			int dy      = my - d->sb_grab_y;
			int new_top = d->sb_grab_top;
			if (travel > 0 && max_top > 0) {
				new_top = d->sb_grab_top + (dy * max_top) / travel;
			}
			dbg_scroll_to(d, new_top);
		}
		return 1;
	}

	if (in_sb && (btn & 0x01) != 0) {
		/* 트랙/thumb 클릭 — 어디든 일단 잡기 시작 */
		d->sb_grab     = 1;
		d->sb_grab_y   = my;
		d->sb_grab_top = d->scroll_top;
		/* thumb 바깥을 클릭했으면 그 위치로 즉시 점프 (page jump) */
		{
			int vis = dbg_visible_lines(d);
			int max_top = (d->count > vis) ? (d->count - vis) : 0;
			int track_h = d->ht;
			int thumb_h = (max_top > 0) ? ((track_h * vis) / d->count) : track_h;
			int rel = my - d->y0 - thumb_h / 2;
			int travel = track_h - thumb_h;
			int target;
			if (travel <= 0 || max_top <= 0) {
				target = 0;
			} else {
				target = (rel * max_top) / travel;
			}
			dbg_scroll_to(d, target);
			d->sb_grab_top = d->scroll_top;
		}
		return 1;
	}

	return 0;
}

void dbg_init(struct SHEET *sht)
{
	int i;
	dbg.sht = sht;
	dbg.bc  = 16 + 1 + 3 * 6 + 4 * 36;
	dbg.x0  = 300;
	dbg.y0  = 480;
	dbg.cx  = 0;
	dbg.cy  = 0;
	dbg.wd  = 400;
	dbg.ht  = 240;
	for (i = 0; i < DBG_MAX_LINES; i++) {
		dbg.lines[i].len = 0;
	}
	/* 시작 시 빈 "현재 작성중 라인" 1개를 미리 잡아둔다 → head=1, count=1.
	 * 그래야 dbg_cur_line() 이 항상 lines[head-1] 를 반환하면 된다.       */
	dbg.head        = 1;
	dbg.count       = 1;
	dbg.scroll_top  = 0;
	dbg.auto_follow = 1;
	dbg.sb_grab     = 0;
	dbg.sb_grab_y   = 0;
	dbg.sb_grab_top = 0;
	make_textbox8(dbg.sht, dbg.x0, dbg.y0, dbg.wd, dbg.ht, dbg.bc);
	dbg_redraw(&dbg);
	sheet_refresh(dbg.sht, dbg.x0 - 3, dbg.y0 - 3,
			dbg.x0 + dbg.wd + 3, dbg.y0 + dbg.ht + 3);
	return;
}
