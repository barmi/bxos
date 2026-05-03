/*
 * 저주소 메모리 슬롯 맵 (4바이트 단위 포인터 / 작은 구조)
 *   0x0fe0  hangul.fnt 시작 주소 (work3 Phase 2 도입, 미적재 시 0)
 *   0x0fe4  shtctl 포인터
 *   0x0fe8  nihongo.fnt 시작 주소
 *   0x0fec  메인 fifo 포인터
 *   0x0ff0  BOOTINFO (16 바이트)
 */

/* asmhead.nas */
struct BOOTINFO { /* 0x0ff0-0x0fff */
	char cyls; /* boot sector는 어디까지 디스크를 읽었는가  */
	char leds; /* 부트시 키보드의  LED상태 */
	char vmode; /* 비디오 모드 몇 비트 컬러인가  */
	char reserve;
	short scrnx, scrny; /* 화면해상도 */
	char *vram;
};
#define ADR_BOOTINFO	0x00000ff0
#define ADR_DISKIMG		0x00100000
#define ADR_DMABUF		0x00268000

/* naskfunc.nas */
void io_hlt(void);
void io_cli(void);
void io_sti(void);
void io_stihlt(void);
int io_in8(int port);
void io_out8(int port, int data);
int io_in16(int port);
void io_out16(int port, int data);
int io_load_eflags(void);
void io_store_eflags(int eflags);
void load_gdtr(int limit, int addr);
void load_idtr(int limit, int addr);
int load_cr0(void);
void store_cr0(int cr0);
void load_tr(int tr);
void asm_inthandler0c(void);
void asm_inthandler0d(void);
void asm_inthandler20(void);
void asm_inthandler21(void);
void asm_inthandler2c(void);
unsigned int memtest_sub(unsigned int start, unsigned int end);
void farjmp(int eip, int cs);
void farcall(int eip, int cs);
void asm_hrb_api(void);
void start_app(int eip, int cs, int esp, int ds, int *tss_esp0);
void asm_end_app(void);

/* fifo.c */
struct FIFO32 {
	int *buf;
	int p, q, size, free, flags;
	struct TASK *task;
};
void fifo32_init(struct FIFO32 *fifo, int size, int *buf, struct TASK *task);
int fifo32_put(struct FIFO32 *fifo, int data);
int fifo32_put_io(struct FIFO32 *fifo, int data);
int fifo32_get(struct FIFO32 *fifo);
int fifo32_status(struct FIFO32 *fifo);

/* graphic.c */
void init_palette(void);
void set_palette(int start, int end, unsigned char *rgb);
void putfont_mask_init(void);	/* work6 Phase 3: 부팅 시 1회 호출 */
void boxfill8(unsigned char *vram, int xsize, unsigned char c, int x0, int y0, int x1, int y1);
void init_screen8(char *vram, int x, int y);
void taskbar_redraw(char *vram, int x, int y, int start_hover, int start_pressed);
void putfont8(char *vram, int xsize, int x, int y, char c, char *font);
void putfonts8_asc(char *vram, int xsize, int x, int y, char c, unsigned char *s);
/* work6 Phase 2: ASCII 전용 fast path. 호출처가 ASCII 만 그릴 게 보장되는
 * (taskbar 라벨, 메뉴 라벨, ASCII 콘솔 출력 등) 곳에서 직접 호출 — langmode
 * 분기 / task_now() 부하 없음. */
void putfonts8_asc_ascii(char *vram, int xsize, int x, int y, char c, unsigned char *s);
void init_mouse_cursor8(char *mouse, char bc);
void putblock8_8(char *vram, int vxsize, int pxsize,
	int pysize, int px0, int py0, char *buf, int bxsize);
#define COL8_000000		0
#define COL8_FF0000		1
#define COL8_00FF00		2
#define COL8_FFFF00		3
#define COL8_0000FF		4
#define COL8_FF00FF		5
#define COL8_00FFFF		6
#define COL8_FFFFFF		7
#define COL8_C6C6C6		8
#define COL8_840000		9
#define COL8_008400		10
#define COL8_848400		11
#define COL8_000084		12
#define COL8_840084		13
#define COL8_008484		14
#define COL8_848484		15

#define TASKBAR_HEIGHT		28
#define TASKBAR_START_X0	2
#define TASKBAR_START_X1	60
#define TASKBAR_START_Y_PAD_TOP	24
#define TASKBAR_START_Y_PAD_BOT	4
#define TASKBAR_TRAY_W		44
#define TASKBAR_TRAY_R_PAD	3
/* work5 Phase 6: Taskbar window list (가운데 영역) — Start 버튼 우측 4px gap
 * 부터 tray 좌측 5px gap 까지. 최대 8개 버튼, 각 버튼은 32..160px. */
#define TASKBAR_WIN_X0		(TASKBAR_START_X1 + 4)
#define TASKBAR_WIN_BTN_MAX	8
#define TASKBAR_WIN_BTN_MIN_W	32
#define TASKBAR_WIN_BTN_MAX_W	160
#define TASKBAR_WIN_BTN_GAP	2
#define TASKBAR_WIN_TRAY_GAP	5

#define MAX_MOUSE_CURSOR	4
#define SIZE_MOUSE_CURSOR	(16*16)
#define BX_CURSOR_ARROW		0
#define BX_CURSOR_RESIZE_NWSE	1
#define BX_CURSOR_RESIZE_WE	2
#define BX_CURSOR_RESIZE_NS	3
void mouse_set_cursor_shape(int shape);

/* dsctbl.c */
struct SEGMENT_DESCRIPTOR {
	short limit_low, base_low;
	char base_mid, access_right;
	char limit_high, base_high;
};
struct GATE_DESCRIPTOR {
	short offset_low, selector;
	char dw_count, access_right;
	short offset_high;
};
void init_gdtidt(void);
void set_segmdesc(struct SEGMENT_DESCRIPTOR *sd, unsigned int limit, int base, int ar);
void set_gatedesc(struct GATE_DESCRIPTOR *gd, int offset, int selector, int ar);
#define ADR_IDT			0x0026f800
#define LIMIT_IDT		0x000007ff
#define ADR_GDT			0x00270000
#define LIMIT_GDT		0x0000ffff
#define ADR_BOTPAK		0x00280000
#define LIMIT_BOTPAK	0x0007ffff
#define AR_DATA32_RW	0x4092
#define AR_CODE32_ER	0x409a
#define AR_LDT			0x0082
#define AR_TSS32		0x0089
#define AR_INTGATE32	0x008e

/* int.c */
void init_pic(void);
#define PIC0_ICW1		0x0020
#define PIC0_OCW2		0x0020
#define PIC0_IMR		0x0021
#define PIC0_ICW2		0x0021
#define PIC0_ICW3		0x0021
#define PIC0_ICW4		0x0021
#define PIC1_ICW1		0x00a0
#define PIC1_OCW2		0x00a0
#define PIC1_IMR		0x00a1
#define PIC1_ICW2		0x00a1
#define PIC1_ICW3		0x00a1
#define PIC1_ICW4		0x00a1

/* keyboard.c */
void inthandler21(int *esp);
void wait_KBC_sendready(void);
void init_keyboard(struct FIFO32 *fifo, int data0);
#define PORT_KEYDAT		0x0060
#define PORT_KEYCMD		0x0064

/* mouse.c */
struct MOUSE_DEC {
	unsigned char buf[3], phase;
	int x, y, btn;
};
void inthandler2c(int *esp);
void enable_mouse(struct FIFO32 *fifo, int data0, struct MOUSE_DEC *mdec);
int mouse_decode(struct MOUSE_DEC *mdec, unsigned char dat);

/* memory.c */
#define MEMMAN_FREES		4090	/* 이것으로 약 32KB */
#define MEMMAN_ADDR			0x003c0000
struct FREEINFO {	/* 빈 정보  */
	unsigned int addr, size;
};
struct MEMMAN {		/* 메모리 매니지먼트 */
	int frees, maxfrees, lostsize, losts;
	struct FREEINFO free[MEMMAN_FREES];
};
unsigned int memtest(unsigned int start, unsigned int end);
void memman_init(struct MEMMAN *man);
unsigned int memman_total(struct MEMMAN *man);
unsigned int memman_alloc(struct MEMMAN *man, unsigned int size);
int memman_free(struct MEMMAN *man, unsigned int addr, unsigned int size);
unsigned int memman_alloc_4k(struct MEMMAN *man, unsigned int size);
int memman_free_4k(struct MEMMAN *man, unsigned int addr, unsigned int size);

/* sheet.c */
#define MAX_SHEETS		256
struct SCROLLWIN;
/* sheet flags (sheet.c 도 같은 비트값 사용) */
#define SHEET_FLAG_USE			0x01
#define SHEET_FLAG_APP_WIN		0x10
#define SHEET_FLAG_HAS_CURSOR	0x20
#define SHEET_FLAG_RESIZABLE	0x40	/* default true (sheet_alloc 에서 set) */
#define SHEET_FLAG_SCROLLWIN	0x80
/* work4 Phase 2: app 가 client area mouse 이벤트를 받겠다고 opt-in 한 창 */
#define SHEET_FLAG_APP_EVENTS	0x100
/* work4 Phase 2: app 가 더블클릭을 커널에서 합성받겠다고 opt-in 한 창 */
#define SHEET_FLAG_APP_DBLCLK	0x200
/* work5 Phase 2: 커널 내부 system widget sheet. 일반 focus/drag/resize 대상 제외. */
#define SHEET_FLAG_SYSTEM_WIDGET	0x400
/* work6 Phase 4: dirty rect 누적 한도. 5번째 add 시 외접 합집합 (union) 으로 합침. */
#define SHEET_DIRTY_MAX	4
struct SHEET {
	unsigned char *buf;
	int bxsize, bysize, vx0, vy0, col_inv, height, flags;
	struct SHTCTL *ctl;
	struct TASK *task;
	struct SCROLLWIN *scroll;
	char title[32];	/* work4 Phase 3: api_openwin 시 저장, api_resizewin 시 frame 재그리기 용 */
	/* work6 Phase 4: 누적 dirty rect (sheet client 좌표). flush 시 한 번에 refresh.
	 * 기본은 호환 mode — sheet_refresh() 가 즉시 flush 하므로 일반 코드에서는
	 * 차이 없음. sheet_dirty_add() 후 명시적 flush 가 필요한 곳 (Phase 5 의
	 * taskbar/scrollwin 부분 redraw) 에서 사용. */
	short dirty_count;
	short dirty_rect[SHEET_DIRTY_MAX][4];	/* x0,y0,x1,y1 (client area) */
};
struct SHTCTL {
	unsigned char *vram, *map;
	int xsize, ysize, top;
	struct SHEET *sheets[MAX_SHEETS];
	struct SHEET sheets0[MAX_SHEETS];
};
struct SHTCTL *shtctl_init(struct MEMMAN *memman, unsigned char *vram, int xsize, int ysize);
struct SHEET *sheet_alloc(struct SHTCTL *ctl);
void sheet_setbuf(struct SHEET *sht, unsigned char *buf, int xsize, int ysize, int col_inv);
void sheet_updown(struct SHEET *sht, int height);
void sheet_refreshmap(struct SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0);
void sheet_refreshsub(struct SHTCTL *ctl, int vx0, int vy0, int vx1, int vy1, int h0, int h1);
void sheet_refresh(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
/* work6 Phase 4: 부분 갱신 (dirty rect) API. 호출자가 여러 작은 영역을 dirty 로
 *   누적 → 한 번에 flush 해서 sheet_refreshsub 호출 횟수 / blit 면적을 줄인다.
 *   - sheet_dirty_add: 새 rect 를 누적. 같은 영역 재추가는 중복 union.
 *     이미 4개 rect 가 차 있으면 새 rect 를 그 중 하나와 외접 합집합으로 병합
 *     (가장 가까운 rect 와 합치는 휴리스틱).
 *   - sheet_dirty_flush: 누적된 rect 를 한 번에 sheet_refresh. count = 0.
 *   - sheet_dirty_flush_all: 모든 sheet 의 dirty 비움 (HariMain idle 직전).
 * 호환: 기존 sheet_refresh() 는 그대로 동작. dirty rect 사용은 opt-in. */
void sheet_dirty_add(struct SHEET *sht, int bx0, int by0, int bx1, int by1);
void sheet_dirty_flush(struct SHEET *sht);
void sheet_dirty_flush_all(struct SHTCTL *ctl);
int  sheet_dirty_pending(struct SHEET *sht);
void sheet_slide(struct SHEET *sht, int vx0, int vy0);
void sheet_free(struct SHEET *sht);
/* 시트 크기 변경. 새 buf 와 새 (xsize,ysize) 로 교체하고 스크린을 갱신.
 * 화면 위치(vx0,vy0)는 유지된다. 호출자가 새 buf 의 그림(프레임 등)을
 * 미리 준비해서 넘겨야 한다. 이전 buf 의 free 책임은 호출자에게 있음.   */
void sheet_resize(struct SHEET *sht, unsigned char *new_buf,
		int new_xsize, int new_ysize);

/* timer.c */
#define MAX_TIMER		500
struct TIMER {
	struct TIMER *next;
	unsigned int timeout;
	char flags, flags2;
	struct FIFO32 *fifo;
	int data;
};
struct TIMERCTL {
	unsigned int count, next;
	struct TIMER *t0;
	struct TIMER timers0[MAX_TIMER];
};
extern struct TIMERCTL timerctl;
void init_pit(void);
struct TIMER *timer_alloc(void);
void timer_free(struct TIMER *timer);
void timer_init(struct TIMER *timer, struct FIFO32 *fifo, int data);
void timer_settime(struct TIMER *timer, unsigned int timeout);
void inthandler20(int *esp);
int timer_cancel(struct TIMER *timer);
void timer_cancelall(struct FIFO32 *fifo);

/* work4 Phase 2: 사용자 앱 이벤트 구조체 / 상수.
 *   - 모든 좌표는 client-area 기준 (title bar 와 frame 을 뺀 상대 좌표).
 *   - button bit0=left, bit1=right, bit2=middle.                              */
struct BX_EVENT {
	int type;
	int win;
	int x, y;
	int button;
	int key;
	int w, h;
};
#define BX_EVENT_KEY			1
#define BX_EVENT_MOUSE_DOWN		2
#define BX_EVENT_MOUSE_UP		3
#define BX_EVENT_MOUSE_MOVE		4
#define BX_EVENT_MOUSE_DBLCLK	5
#define BX_EVENT_RESIZE			6

/* api_set_winevent flags */
#define BX_WIN_EV_MOUSE			0x01
#define BX_WIN_EV_RESIZE		0x02
#define BX_WIN_EV_DBLCLK		0x04

/* fifo wakeup marker (large enough to not collide with key+256/4 등 기존 값) */
#define BX_EVENT_FIFO_MARKER	0x10000

#define BX_EVENT_QUEUE_LEN		32

/* mtask.c */
#define MAX_PATH	128
#define FS_MAX_DEPTH	16
#define FS_RESOLVE_NO_DISK		-1
#define FS_RESOLVE_BAD_PATH		-2
#define FS_RESOLVE_TOO_LONG		-3
#define FS_RESOLVE_NOT_FOUND	-4
#define FS_RESOLVE_NOT_DIR		-5
#define MAX_TASKS		1000	/* 최대 태스크 수  */
#define TASK_GDT0		3		/* TSS를 GDT의 몇 번부터 할당할 것인가  */
#define MAX_TASKS_LV	100
#define MAX_TASKLEVELS	10
struct TSS32 {
	int backlink, esp0, ss0, esp1, ss1, esp2, ss2, cr3;
	int eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
	int es, cs, ss, ds, fs, gs;
	int ldtr, iomap;
};
struct TASK {
	int sel, flags; /* sel은 GDT 번호 */
	int level, priority;
	struct FIFO32 fifo;
	struct TSS32 tss;
	struct SEGMENT_DESCRIPTOR ldt[2];
	struct CONSOLE *cons;
	int ds_base, cons_stack;
	struct FILEHANDLE *fhandle;
	struct DIRHANDLE *dhandle;     /* work4: 사용자 dir handle 슬롯 */
	struct BX_EVENT *event_buf;    /* work4 Phase 2: app event 큐 (circular) */
	int event_size, event_count, event_p;
	int *fat;
	char *cmdline;
	unsigned char langmode, langbyte1, langbyte2, app_type;
	char name[16];
	unsigned int time;
	unsigned int cwd_clus;
	char cwd_path[MAX_PATH];
};
#define TASK_APP_SYSTEM		0
#define TASK_APP_CONSOLE	1
#define TASK_APP_WINDOW		2
struct TASKLEVEL {
	int running; /* 동작하고 있는 태스크 수  */
	int now; /* 현재 동작하고 있는 태스크의 번호 */
	struct TASK *tasks[MAX_TASKS_LV];
};
struct TASKCTL {
	int now_lv; /* 현재 동작 중의 레벨  */
	char lv_change; /* 다음 번 태스크 스위치 시, 레벨도 바꾸는 편이 좋은 지 판단 */
	struct TASKLEVEL level[MAX_TASKLEVELS];
	struct TASK tasks0[MAX_TASKS];
	int alloc, alive;
};
extern struct TASKCTL *taskctl;
extern struct TIMER *task_timer;
struct TASK *task_now(void);
struct TASK *task_init(struct MEMMAN *memman);
struct TASK *task_alloc(void);
void task_run(struct TASK *task, int level, int priority);
void task_switch(void);
void task_sleep(struct TASK *task);
void task_free(struct TASK *task);
void taskmgr_task(unsigned int memtotal);

/* window.c */
struct MENU {
	int level;
	char name[16], exec[32];
	struct MENU *next, *sub;
};

/* menu.c — work5 Start Menu primitive */
#define KMENU_MAX_ITEMS		64
#define KMENU_MAX_MENUS		8
#define KMENU_ITEM_H		22
#define KMENU_W			200
#define KMENU_LABEL_MAX		24
#define KMENU_ARG_MAX		64
#define KMENU_SECTION_MAX	32
#define KMENU_FLAG_SEPARATOR	0x01
#define KMENU_FLAG_DISABLED	0x02
#define KMENU_FLAG_SUBMENU	0x04
enum {
	KMENU_HANDLER_NONE = 0,
	KMENU_HANDLER_EXEC,
	KMENU_HANDLER_BUILTIN,
	KMENU_HANDLER_SETTINGS,
	KMENU_HANDLER_SUBMENU
};
struct MENU_ITEM {
	char label[KMENU_LABEL_MAX];
	int handler_id;
	int submenu;
	int flags;
	char arg[KMENU_ARG_MAX];
	char hotkey;	/* work5 Phase 7: 0=없음, lowercased ASCII letter/digit */
};
struct KERNEL_MENU {
	struct SHEET *sht;
	struct MENU_ITEM items[KMENU_MAX_ITEMS];
	int n_items, selected;
	struct KERNEL_MENU *parent, *child;
	int x, y, w, h, level;
};
void make_window8(unsigned char *buf, int xsize, int ysize, char *title, char act);
void make_menu8(unsigned char *buf, int xsize, int ysize, char *title, struct MENU *menu, int num);
void putfonts8_asc_sht(struct SHEET *sht, int x, int y, int c, int b, char *s, int l);
void make_textbox8(struct SHEET *sht, int x0, int y0, int sx, int sy, int c);
void make_header8(struct SHEET *sht, int x0, int y0, int sx, int sy, int c);
void make_wtitle8(unsigned char *buf, int xsize, char *title, char act);
void make_mtitle8(unsigned char *buf, int xsize, char *title, char act);
void change_wtitle8(struct SHEET *sht, char act);
void change_mtitle8(struct SHEET *sht, int level, int mn_flg, char act);

/* console.c */
#define CONS_CMDLINE_MAX	30
#define CONS_HISTORY_MAX	16
#define CONS_KEY_UP			0x80
#define CONS_KEY_DOWN		0x81
#define CONS_KEY_LEFT		0x82
#define CONS_KEY_RIGHT		0x83
#define HE2_FLAG_SUBSYSTEM_MASK	0x00000003
#define HE2_SUBSYSTEM_CONSOLE	0x00000000
#define HE2_SUBSYSTEM_WINDOW	0x00000001

struct CONSOLE {
	struct SHEET *sht;
	struct SCROLLWIN *scroll;
	int cur_x, cur_y;		// cursor position
	int cur_c;				// cursor color
	struct TIMER *timer;
	// skshin
	char *linebuf;			// 문자를 내용을 저장(화면에 보이는 부분을 포함하는 전체 버퍼)
	int cwidth, cheight;	// 콘솔의 문자단위 W,H
	int scroll_y;			// 현재 스크롤되어 있는 위치
	int bgcolor;
	int width, height;
	unsigned int cwd_clus;
	char cwd_path[MAX_PATH];
};

/* 공통 스크롤 텍스트 영역 — console/debug 모두 사용 */
#define SCROLL_MAX_LINES   512
#define SCROLL_LINE_CHARS  100
#define SCROLLBAR_W        10

struct SCROLLLINE {
	unsigned char text[SCROLL_LINE_CHARS];
	unsigned char color[SCROLL_LINE_CHARS];
	short len;	/* 실제 문자수 (≤ DBG_LINE_CHARS) */
};

struct SCROLLWIN {
	struct SHEET *sht;
	int bc;
	int x0, y0;
	int wd, ht;
	int cx;
	unsigned char langmode;
	struct SCROLLLINE lines[SCROLL_MAX_LINES];
	int head;
	int count;
	int scroll_top;
	int auto_follow;
	int sb_grab;
	int sb_grab_y;
	int sb_grab_top;
};

/* 디버그 창 */
struct DBGWIN {
	struct SHEET *sht;
	struct SCROLLWIN sw;
};

void console_task(struct SHEET *sheet, int memtotal);
void cons_putchar(struct CONSOLE *cons, int chr, char move);
void cons_newline(struct CONSOLE *cons);
void cons_putstr0(struct CONSOLE *cons, char *s);
void cons_putstr1(struct CONSOLE *cons, char *s, int l);
void cons_runcmd(char *cmdline, struct CONSOLE *cons, int *fat, int memtotal);
void cmd_mem(struct CONSOLE *cons, int memtotal);
void cmd_bench(struct CONSOLE *cons, char *cmdline);
void cmd_cls(struct CONSOLE *cons);
void cmd_dir(struct CONSOLE *cons, char *cmdline);
void cmd_task(void);
void cmd_disk(struct CONSOLE *cons);
void cmd_resolve(struct CONSOLE *cons, char *cmdline);
void cmd_mkdir(struct CONSOLE *cons, char *cmdline);
void cmd_rmdir(struct CONSOLE *cons, char *cmdline);
void cmd_cd(struct CONSOLE *cons, char *cmdline);
void cmd_pwd(struct CONSOLE *cons);
void cmd_touch(struct CONSOLE *cons, char *cmdline);
void cmd_rm(struct CONSOLE *cons, char *cmdline);
void cmd_cp(struct CONSOLE *cons, char *cmdline);
void cmd_mv(struct CONSOLE *cons, char *cmdline);
void cmd_echo(struct CONSOLE *cons, char *cmdline);
void cmd_mkfile(struct CONSOLE *cons, char *cmdline);
void cmd_exit(struct CONSOLE *cons, int *fat);
void cmd_start(struct CONSOLE *cons, char *cmdline, int *fat, int memtotal);
void cmd_ncst(struct CONSOLE *cons, char *cmdline, int memtotal);
void cmd_langmode(struct CONSOLE *cons, char *cmdline);
int cmd_app(struct CONSOLE *cons, int *fat, char *cmdline);
int *hrb_api(int *reg, int edi, int esi, int ebp, int esp, int ebx, int edx, int ecx, int eax);
/* work4 Phase 2: BX_EVENT 한 개를 task 의 event 큐에 push 하고 task 를 깨운다.
 * 큐가 가득 찼으면 가장 오래된 항목을 폐기하지 않고 새 이벤트를 drop 한다.   */
void bx_event_post(struct TASK *task, int type, int win, int x, int y,
		int button, int key, int w, int h);
int *inthandler0d(int *esp);
int *inthandler0c(int *esp);
void hrb_api_linewin(struct SHEET *sht, int x0, int y0, int x1, int y1, int col);
void scrollwin_init(struct SCROLLWIN *sw, struct SHEET *sht,
		int x0, int y0, int wd, int ht, int bc);
void scrollwin_putc(struct SCROLLWIN *sw, int chr, int fc);
void scrollwin_puts(struct SCROLLWIN *sw, char *s, int c);
void scrollwin_redraw(struct SCROLLWIN *sw);
void scrollwin_scroll_to(struct SCROLLWIN *sw, int new_top);
int  scrollwin_handle_mouse(struct SCROLLWIN *sw, int mx, int my, int btn);
int  scrollwin_cursor_x(struct SCROLLWIN *sw);
int  scrollwin_cursor_y(struct SCROLLWIN *sw);
void scrollwin_backspace(struct SCROLLWIN *sw);
int  scrollwin_text_cols(struct SCROLLWIN *sw);
void dbg_init(struct SHTCTL *shtctl);
void dbg_open(void);
void dbg_newline(struct DBGWIN *dbg);
void dbg_putstr0(char *s, int c);
void dbg_putstr1(char *s, int l, int c);
/* 화면 redraw (현재 scroll_top 기준으로 보이는 라인들을 그림) */
void dbg_redraw(struct DBGWIN *dbg);
/* 절대 위치로 스크롤. clamp 됨. follow 모드는 끝으로 갔을 때 다시 ON. */
void dbg_scroll_to(struct DBGWIN *dbg, int new_top);
/* 마우스 이벤트 hook — bootpack.c 의 메인 마우스 루프에서 호출.
 * 반환값 1 = 이 이벤트는 dbg 가 소비함 (다른 윈도우 처리 skip).         */
int  dbg_handle_mouse(struct DBGWIN *dbg, int mx, int my, int btn);
/* 외부에서 dbg 핸들 가져오기 (bootpack.c 가 사용). console.c 의 static . */
struct DBGWIN *dbg_get(void);

/* file.c */
struct FILEINFO {
	unsigned char name[8], ext[3], type;
	char reserve[10];
	unsigned short time, date, clustno;
	unsigned int size;
};

void file_readfat(int *fat, unsigned char *img);
void file_loadfile(int clustno, int size, char *buf, int *fat, char *img);
struct FILEINFO *file_search(char *name, struct FILEINFO *finfo, int max);
char *file_loadfile2(int clustno, int *psize, int *fat);

/* fs_fat.c — FAT12/FAT16 마운트 + ATA 기반 read (work1 Phase 3).
 * 데이터 드라이브(=ATA master, FAT16) 한 개만 다룬다. */
struct FS_MOUNT {
	int drive;
	int fs_type;                  /* 12 or 16 */
	unsigned int total_sectors;
	int sectors_per_cluster;
	int reserved_sectors;
	int num_fats;
	int sectors_per_fat;
	int root_entries;
	int root_sectors;
	unsigned int cluster_count;
	unsigned int fat_lba;
	unsigned int root_lba;
	unsigned int data_lba;
	unsigned char *fat_cache;
	struct FILEINFO *root_cache;
};
struct DIR_SLOT {
	unsigned int lba;
	unsigned short offset;
	struct FILEINFO *cache_entry;
};
struct FS_FILE {
	struct FILEINFO finfo;
	struct DIR_SLOT slot;
};
struct FILEHANDLE {
	char *buf;
	int size;
	int pos;
	int mode;                 /* 0=free, 1=read buffer, 2=write-through */
	struct FILEINFO *finfo;   /* legacy root write handle target */
	struct FS_FILE file;      /* path-aware write handle target */
};
struct DIR_ITER {
	unsigned int dir_clus;        /* 0 = root directory */
	unsigned int cur_lba;
	unsigned int cur_offset_in_sector;
	unsigned int cur_cluster_offset;
	unsigned int cur_clus;
	unsigned int entry_index;
	unsigned char sector[512];
	int sector_loaded;
	int at_end;
};
/* 사용자 앱이 보는 디렉터리 엔트리 (work4 Phase 1). 커널 FILEINFO 의 일부를
 * 노출용으로 압축한 것. attr=0x10 → 디렉터리.                                */
struct BX_DIRINFO {
	char         name[13];        /* "NAME.EXT" + NUL, 디렉터리는 name only */
	unsigned char attr;           /* FAT attr (FILEINFO.type) */
	unsigned int size;
	unsigned int clustno;
};
#define DIR_HANDLES_PER_TASK	4
struct DIRHANDLE {
	int in_use;
	struct DIR_ITER it;
};
extern struct FS_MOUNT g_data_mount;
extern int g_data_mounted;
int fs_mount_data(int drive);
struct FILEINFO *fs_data_root(void);
int fs_data_root_max(void);
int dir_iter_open(struct DIR_ITER *it, unsigned int dir_clus);
int dir_iter_next(struct DIR_ITER *it, struct FILEINFO *entry, struct DIR_SLOT *slot_addr);
void dir_iter_close(struct DIR_ITER *it);
int dir_find(unsigned int parent_clus, unsigned char name83[11],
		struct FILEINFO *finfo, struct DIR_SLOT *slot_addr);
int dir_alloc_slot(unsigned int parent_clus, struct DIR_SLOT *slot_addr);
int dir_write_slot(struct DIR_SLOT *slot_addr, struct FILEINFO *entry);
int fs_resolve_path(unsigned int start_clus, char *path,
		unsigned int *parent_clus, unsigned char leaf_name83[11],
		struct FILEINFO *leaf_finfo, struct DIR_SLOT *leaf_slot);
int fs_mkdir(unsigned int start_clus, char *path);
int fs_rmdir(unsigned int start_clus, char *path);
int fs_data_open_path(unsigned int start_clus, char *path, struct FS_FILE *file);
int fs_data_create_path(unsigned int start_clus, char *path, struct FS_FILE *file);
int fs_file_read(struct FS_FILE *file, int pos, void *buf, int n);
int fs_file_write(struct FS_FILE *file, int pos, const void *buf, int n);
int fs_file_truncate(struct FS_FILE *file, int size);
int fs_file_unlink(struct FS_FILE *file);
struct FILEINFO *fs_data_search(char *name);
char *fs_data_loadfile(int clustno, int *psize);
int fs_data_read(struct FILEINFO *finfo, int pos, void *buf, int n);
int fs_data_create(char *name);
int fs_data_write(struct FILEINFO *finfo, int pos, const void *buf, int n);
int fs_data_truncate(struct FILEINFO *finfo, int size);
int fs_data_unlink(struct FILEINFO *finfo);

/* work4 Phase 1: 사용자 API 용 안전 래퍼.
 *   - fs_user_opendir / readdir / closedir 는 DIR_ITER 를 직접 노출하지 않고
 *     커널 측 DIRHANDLE 슬롯을 통해 호출된다.
 *   - readdir 는 deleted entry, volume label, LFN slot 을 자동으로 skip 하고
 *     `.` / `..` 도 사용자 앱에는 그대로 전달한다(트리 표시에 필요).
 *   - 반환값 규칙: 1=entry 1개, 0=end, -1=error.                              */
int fs_user_opendir(unsigned int start_clus, char *path, struct DIR_ITER *it);
int fs_user_readdir(struct DIR_ITER *it, struct BX_DIRINFO *out);
int fs_user_stat(unsigned int start_clus, char *path, struct BX_DIRINFO *out);
int fs_user_rename(unsigned int start_clus, char *oldpath, char *newpath);
void fs_dirinfo_from_finfo(const struct FILEINFO *src, struct BX_DIRINFO *dst);


/* tek.c */
int tek_getsize(unsigned char *p);
int tek_decomp(unsigned char *p, char *q, int size);

/* bootpack.c */
extern unsigned int g_memtotal;     /* HariMain memtest 결과 캐시 (work4: api_exec 용) */
extern struct SHEET *g_sht_mouse;   /* system sheets place menus directly below cursor */
extern struct SHEET *g_sht_back;    /* work5 Phase 6: 데스크톱/taskbar background sheet */
extern unsigned char *g_buf_back;   /* work5 Phase 6: g_sht_back 의 backing buffer */
extern int g_background_color;       /* work5 Phase 1: desktop background color */
extern int g_default_langmode;        /* work5 Phase 5: boot default langmode */
extern int g_start_menu_open;        /* work5 Phase 1: Start button/menu toggle state */
extern int g_clock_seconds;          /* work5 Phase 5: uptime clock, seconds since 00:00 */
extern int g_clock_show_seconds;     /* work5 Phase 5: tray clock format */
extern int g_clock_minutes;          /* work5 Phase 4 compat: minutes since 00:00 */
void start_menu_init(struct SHTCTL *shtctl, struct MEMMAN *memman, int scrnx, int scrny);
void start_menu_toggle(void);
void start_menu_close_all(void);
int start_menu_is_open(void);
int start_menu_handle_key(int key);
int start_menu_handle_char(char chr);
int start_menu_handle_mouse(int mx, int my, int btn, int old_btn);
void start_menu_dispatch(struct MENU_ITEM *item);

/* work5 Phase 6: Taskbar 윈도우 목록 / Alt+Tab.
 *   taskbar_full_redraw 는 background fill + Start + tray + 윈도우 목록 버튼을
 *   한 번에 다시 그리고 sht_back 을 refresh 한다.
 *   taskbar_mark_dirty 는 다음 idle 진입 직전에 redraw 가 필요함을 표시한다.
 *   alt_tab_cycle(prev) 은 visible app sheet 들 중 다음/이전을 focus 로 올린다. */
void taskbar_full_redraw(int start_hover, int start_pressed);
/* work6 Phase 5: partial refresh — buf_back 는 그대로 다 그리지만 (row memset
 * 빠름), sheet_refresh rect 만 좁혀 blit 비용 절감. 시계 tick 같은 high-freq
 * hot path 에서 효과적. */
void taskbar_redraw_clock_only(void);
void taskbar_redraw_start_only(int start_hover, int start_pressed);
void taskbar_mark_dirty(void);
void alt_tab_cycle(int prev);
int taskbar_winlist_hit(int mx, int my);
struct SHEET *taskbar_winlist_sheet_at(int idx);
#define MAX_MENU		256
#define MAX_MNLV		  8
struct MNLV {
	struct MENU *menu;
	struct SHEET *sht;
	unsigned short *buf;
	int pos, num;
};
void keywin_on(struct SHEET *key_win);
struct TASK *open_constask(struct SHEET *sht, unsigned int memtotal);
struct SHEET *open_console(struct SHTCTL *shtctl, unsigned int memtotal);
void system_request_keywin(struct SHEET *sht);
void system_start_command(char *cmdline, unsigned int memtotal);
void open_taskmgr(unsigned int memtotal);
/* console 윈도우를 새 크기로 리사이즈. 새 buffer 를 alloc 해서 sheet_resize.
 * 기존 buffer 는 free 함. 호출 후 sht 의 buf/bxsize/bysize 가 갱신됨.    */
void console_resize(struct SHEET *sht, int new_w, int new_h);

/* ata.c — Primary IDE / ATA PIO 드라이버 (work1 Phase 2). */
struct ATA_INFO {
	int present;                  /* 0 = 디바이스 없음/오류, 1 = OK */
	unsigned int total_sectors;   /* LBA28 sectors (max 0x0FFFFFFF) */
	char model[41];               /* IDENTIFY 응답의 모델 문자열 (공백 트림) */
};
extern struct ATA_INFO ata_drive_info[2];   /* [0]=master, [1]=slave */
void ata_init(void);
int ata_identify(int drive, struct ATA_INFO *out);
int ata_read_sectors(int drive, unsigned int lba, int count, void *buf);
int ata_write_sectors(int drive, unsigned int lba, int count, const void *buf);
