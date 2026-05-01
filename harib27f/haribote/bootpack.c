/* bootpack의 메인 */

#include "bootpack.h"
#include "bench.h"
#include <stdio.h>
#include <string.h>

#define KEYCMD_LED		0xed
#define TIMER_CLOCK		2281
#define SETTINGS_CFG_PATH	"/SYSTEM/SETTINGS.CFG"
#define SETTINGS_CFG_BUF_MAX	512
#define RUN_DIALOG_W	320
#define RUN_DIALOG_H	100
#define ABOUT_DIALOG_W	280
#define ABOUT_DIALOG_H	160

struct TASK *fdc, *inout, *taskmgr;

void keywin_off(struct SHEET *key_win);
void close_console(struct SHEET *sht);
void close_constask(struct TASK *task);
void close_taskmgr(void);
void init_menu(struct MNLV *mnlv, struct MENU **menu);
void scrollwin_window_resize(struct SHEET *sht, int new_w, int new_h, char *title);

unsigned int g_memtotal = 0;	/* HariMain memtest 결과 — work4 api_exec 등에서 참조 */
struct SHEET *g_sht_mouse = 0;
struct SHEET *g_sht_back = 0;
unsigned char *g_buf_back = 0;
unsigned char *g_mouse_buf = 0;
int g_mouse_cursor = BX_CURSOR_ARROW;
static int g_taskbar_dirty = 0;
static int g_taskbar_start_hover = 0;
static int g_taskbar_start_pressed = 0;
/* work5 Phase 6: 윈도우 목록 버튼 layout 캐시. taskbar_full_redraw 시 갱신,
 * mouse hit-test / Alt+Tab 가 같은 데이터를 참조한다.                       */
struct TASKBAR_WIN_BTN {
	struct SHEET *sht;
	int x0, x1;
};
static struct TASKBAR_WIN_BTN g_taskbar_btns[TASKBAR_WIN_BTN_MAX];
static int g_taskbar_btn_count = 0;
static int g_taskbar_btn_hover = -1;
static int g_taskbar_btn_pressed = -1;

struct SYSTEM_DIALOG {
	struct SHEET *sht;
	unsigned char *buf;
	int w, h;
	char input[CONS_CMDLINE_MAX];
	int len;
	int focus;	/* work5 Phase 7: 0=input, 1=OK, 2=Cancel */
};

static struct SYSTEM_DIALOG g_run_dialog = { 0 };
static struct SYSTEM_DIALOG g_about_dialog = { 0 };
static struct SHEET *g_pending_key_win = 0;
static char g_settings_cfg_buf[SETTINGS_CFG_BUF_MAX];

static char *settings_trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
		s++;
	}
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
		*--e = 0;
	}
	return s;
}

static int settings_streq(char *a, char *b)
{
	return strcmp(a, b) == 0;
}

static void settings_warn(const char *key, const char *value, const char *msg)
{
	char buf[96];
	int n = 0;
	const char *prefix = "[settings] ";
	while (*prefix && n < (int) sizeof(buf) - 1) buf[n++] = *prefix++;
	while (key && *key && n < (int) sizeof(buf) - 4) buf[n++] = *key++;
	if (value != 0) {
		buf[n++] = ' '; buf[n++] = '=';  buf[n++] = ' ';
		while (*value && n < (int) sizeof(buf) - 4) buf[n++] = *value++;
	}
	buf[n++] = ':'; buf[n++] = ' ';
	while (msg && *msg && n < (int) sizeof(buf) - 2) buf[n++] = *msg++;
	buf[n++] = '\n';
	buf[n] = 0;
	dbg_putstr0(buf, COL8_FF0000);
	return;
}

static void settings_apply_pair(char *key, char *value)
{
	if (settings_streq(key, "display.background")) {
		if (settings_streq(value, "black")) {
			g_background_color = COL8_000000;
		} else if (settings_streq(value, "gray")) {
			g_background_color = COL8_848484;
		} else if (settings_streq(value, "green")) {
			g_background_color = COL8_008400;
		} else if (settings_streq(value, "navy")) {
			g_background_color = COL8_008484;
		} else {
			g_background_color = COL8_008484;
			settings_warn(key, value, "unknown color, fell back to navy");
		}
	} else if (settings_streq(key, "language.mode")) {
		if ('0' <= value[0] && value[0] <= '4' && value[1] == 0) {
			g_default_langmode = value[0] - '0';
		} else {
			settings_warn(key, value, "expected 0..4");
		}
	} else if (settings_streq(key, "time.boot_offset_min")) {
		int i, v = 0;
		for (i = 0; '0' <= value[i] && value[i] <= '9'; i++) {
			v = v * 10 + value[i] - '0';
		}
		if (value[i] == 0 && 0 <= v && v < 24 * 60) {
			g_clock_seconds = v * 60;
			g_clock_minutes = v;
		} else {
			settings_warn(key, value, "expected 0..1439 minutes");
		}
	} else if (settings_streq(key, "time.show_seconds")) {
		if (settings_streq(value, "true") || settings_streq(value, "1")) {
			g_clock_show_seconds = 1;
		} else if (settings_streq(value, "false") || settings_streq(value, "0")) {
			g_clock_show_seconds = 0;
		} else {
			settings_warn(key, value, "expected true/false");
		}
	} else {
		settings_warn(key, value, "unknown key");
	}
	return;
}

static void system_settings_load_boot(void)
{
	struct FS_FILE file;
	int size, n;
	char *p, *line, *eq, *end, *key, *value;

	if (fs_data_open_path(0, SETTINGS_CFG_PATH, &file) != 0) {
		return;
	}
	size = file.finfo.size;
	if (size <= 0 || size >= SETTINGS_CFG_BUF_MAX) {
		return;
	}
	n = fs_file_read(&file, 0, g_settings_cfg_buf, size);
	if (n != size) {
		return;
	}
	g_settings_cfg_buf[size] = 0;
	p = g_settings_cfg_buf;
	while (*p != 0) {
		line = p;
		while (*p != 0 && *p != '\n') {
			p++;
		}
		if (*p == '\n') {
			*p++ = 0;
		}
		for (end = line; *end != 0; end++) {
			if (*end == '#' || *end == ';') {
				*end = 0;
				break;
			}
		}
		line = settings_trim(line);
		if (line[0] == 0) {
			continue;
		}
		eq = line;
		while (*eq != 0 && *eq != '=') {
			eq++;
		}
		if (*eq != '=') {
			continue;
		}
		*eq = 0;
		key = settings_trim(line);
		value = settings_trim(eq + 1);
		settings_apply_pair(key, value);
	}
	return;
}

static void system_raise_sheet(struct SHEET *sht)
{
	int h;
	if (sht == 0 || sht->ctl == 0) {
		return;
	}
	h = sht->ctl->top + 1;
	if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
		h = g_sht_mouse->height;
	}
	sheet_updown(sht, h);
	if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
		if (g_sht_mouse->height != sht->ctl->top) {
			sheet_updown(g_sht_mouse, sht->ctl->top);
		}
		sheet_slide(g_sht_mouse, g_sht_mouse->vx0, g_sht_mouse->vy0);
	}
	return;
}

void system_request_keywin(struct SHEET *sht)
{
	g_pending_key_win = sht;
	taskbar_mark_dirty();
	return;
}

/* work5 Phase 6: Taskbar 윈도우 목록 helpers --------------------------------- */

void taskbar_mark_dirty(void)
{
	g_taskbar_dirty = 1;
	return;
}

static int taskbar_winlist_eligible(struct SHEET *sht)
{
	if (sht == 0) return 0;
	if ((sht->flags & SHEET_FLAG_USE) == 0) return 0;
	if (sht->height < 1) return 0;
	if ((sht->flags & SHEET_FLAG_SYSTEM_WIDGET) != 0) return 0;
	if (sht == g_sht_mouse) return 0;
	if (sht == g_sht_back) return 0;
	/* APP_WIN(HE2 앱) / HAS_CURSOR(콘솔) / SCROLLWIN(debug) / 시스템 task 창
	 * 만 목록에 포함. sht_back / sht_mouse / 메뉴 sheet 등은 위에서 걸러짐.   */
	if ((sht->flags & SHEET_FLAG_APP_WIN) != 0) return 1;
	if ((sht->flags & SHEET_FLAG_HAS_CURSOR) != 0) return 1;
	if ((sht->flags & SHEET_FLAG_SCROLLWIN) != 0) return 1;
	if (sht->task != 0) return 1;
	return 0;
}

static void taskbar_winlist_label(struct SHEET *sht, char *out, int n)
{
	int i;
	const char *src = 0;
	if ((sht->flags & SHEET_FLAG_APP_WIN) != 0 && sht->title[0] != 0) {
		src = sht->title;
	} else if ((sht->flags & SHEET_FLAG_HAS_CURSOR) != 0) {
		src = "Console";
	} else if (sht->task != 0 && sht->task->name[0] != 0) {
		src = sht->task->name;
	} else if ((sht->flags & SHEET_FLAG_SCROLLWIN) != 0) {
		src = "Debug";
	} else {
		src = "?";
	}
	for (i = 0; i < n - 1 && src[i] != 0; i++) {
		out[i] = src[i];
	}
	out[i] = 0;
	return;
}

/* sht_back 의 background buffer 에 한 버튼을 그린다. focused=1 → sunken,
 * pressed=1 → sunken (focus 와 같은 모양), hover=1 → 상단 highlight 1px.    */
static void taskbar_winlist_draw_button(unsigned char *vram, int scrnx, int scrny,
		int x0, int x1, char *label, int focused, int hover, int pressed)
{
	int sy0 = scrny - TASKBAR_START_Y_PAD_TOP;
	int sy1 = scrny - TASKBAR_START_Y_PAD_BOT;
	int sunken = focused || pressed;
	int label_x, label_y;
	int label_max_chars, label_len;
	char buf[32];
	int i;

	boxfill8(vram, scrnx, COL8_C6C6C6, x0, sy0, x1, sy1);
	if (sunken) {
		boxfill8(vram, scrnx, COL8_000000, x0,     sy0,     x1,     sy0);
		boxfill8(vram, scrnx, COL8_000000, x0,     sy0,     x0,     sy1);
		boxfill8(vram, scrnx, COL8_848484, x0 + 1, sy0 + 1, x1 - 1, sy0 + 1);
		boxfill8(vram, scrnx, COL8_848484, x0 + 1, sy0 + 1, x0 + 1, sy1 - 1);
		boxfill8(vram, scrnx, COL8_FFFFFF, x0 + 1, sy1 - 1, x1 - 1, sy1 - 1);
		boxfill8(vram, scrnx, COL8_FFFFFF, x1 - 1, sy0 + 1, x1 - 1, sy1 - 1);
	} else {
		boxfill8(vram, scrnx, COL8_FFFFFF, x0 + 1, sy0,     x1 - 1, sy0);
		boxfill8(vram, scrnx, COL8_FFFFFF, x0,     sy0,     x0,     sy1);
		boxfill8(vram, scrnx, COL8_848484, x0 + 1, sy1,     x1 - 1, sy1);
		boxfill8(vram, scrnx, COL8_848484, x1 - 1, sy0 + 1, x1 - 1, sy1 - 1);
		boxfill8(vram, scrnx, COL8_000000, x0,     sy1 + 1, x1 - 1, sy1 + 1);
		boxfill8(vram, scrnx, COL8_000000, x1,     sy0,     x1,     sy1 + 1);
		if (hover) {
			boxfill8(vram, scrnx, COL8_FFFFFF, x0 + 4, sy0 + 3, x1 - 5, sy0 + 3);
		}
	}
	label_max_chars = ((x1 - x0) - 8) / 8;
	if (label_max_chars < 1) label_max_chars = 1;
	if (label_max_chars > (int) sizeof(buf) - 1) label_max_chars = sizeof(buf) - 1;
	label_len = 0;
	while (label[label_len] != 0) label_len++;
	if (label_len > label_max_chars) {
		for (i = 0; i < label_max_chars && i < (int) sizeof(buf) - 1; i++) {
			buf[i] = label[i];
		}
		if (label_max_chars >= 1) buf[label_max_chars - 1] = '.';
		buf[label_max_chars] = 0;
	} else {
		for (i = 0; i < label_len; i++) {
			buf[i] = label[i];
		}
		buf[label_len] = 0;
	}
	label_x = x0 + 5 + (sunken ? 1 : 0);
	label_y = scrny - 21 + (sunken ? 1 : 0);
	{
		extern char hankaku[4096];
		char *p = buf;
		while (*p != 0) {
			putfont8((char *) vram, scrnx, label_x, label_y,
					COL8_000000, hankaku + ((unsigned char) *p) * 16);
			label_x += 8;
			p++;
		}
	}
	return;
}

/* 빈 영역(background 색)을 채워 이전 frame 의 버튼 잔상을 지운다. */
static void taskbar_winlist_clear(unsigned char *vram, int scrnx, int scrny,
		int x0, int x1)
{
	int sy0 = scrny - TASKBAR_START_Y_PAD_TOP;
	int sy1 = scrny - TASKBAR_START_Y_PAD_BOT + 1;
	if (x0 > x1) return;
	boxfill8(vram, scrnx, COL8_C6C6C6, x0, sy0, x1, sy1);
	return;
}

static int taskbar_winlist_collect(struct SHTCTL *ctl, struct SHEET **out, int max_n)
{
	int i, n = 0;
	struct SHEET *sht;
	if (ctl == 0) return 0;
	for (i = 0; i < MAX_SHEETS && n < max_n; i++) {
		sht = &ctl->sheets0[i];
		if (taskbar_winlist_eligible(sht)) {
			out[n++] = sht;
		}
	}
	return n;
}

static int taskbar_winlist_layout(int scrnx, int n, int *out_btn_w)
{
	int tray_w = g_clock_show_seconds ? 68 : TASKBAR_TRAY_W;
	int tray_left = scrnx - TASKBAR_TRAY_R_PAD - tray_w;
	int avail_x1 = tray_left - TASKBAR_WIN_TRAY_GAP;
	int avail = avail_x1 - TASKBAR_WIN_X0 + 1;
	int btn_w;
	if (n <= 0 || avail <= TASKBAR_WIN_BTN_MIN_W) {
		*out_btn_w = 0;
		return 0;
	}
	btn_w = (avail - (n - 1) * TASKBAR_WIN_BTN_GAP) / n;
	if (btn_w > TASKBAR_WIN_BTN_MAX_W) btn_w = TASKBAR_WIN_BTN_MAX_W;
	if (btn_w < TASKBAR_WIN_BTN_MIN_W) {
		/* 너무 빡빡 — 가능한 만큼만 그린다 */
		int max_n = (avail + TASKBAR_WIN_BTN_GAP) /
				(TASKBAR_WIN_BTN_MIN_W + TASKBAR_WIN_BTN_GAP);
		if (max_n < 1) max_n = 1;
		if (max_n > n) max_n = n;
		btn_w = (avail - (max_n - 1) * TASKBAR_WIN_BTN_GAP) / max_n;
		if (btn_w > TASKBAR_WIN_BTN_MAX_W) btn_w = TASKBAR_WIN_BTN_MAX_W;
		*out_btn_w = btn_w;
		return max_n;
	}
	*out_btn_w = btn_w;
	return n;
}

void taskbar_full_redraw(int start_hover, int start_pressed)
{
	struct SHTCTL *ctl;
	struct SHEET *key_win_now = 0;
	struct SHEET *sheets[TASKBAR_WIN_BTN_MAX];
	char label[32];
	int n, i, btn_w, x;
	int prev_clear_x0;
	int scrnx, scrny;

	if (g_sht_back == 0 || g_buf_back == 0) {
		return;
	}
	bench_enter(BENCH_TASKBAR);
	g_taskbar_start_hover = start_hover;
	g_taskbar_start_pressed = start_pressed;
	scrnx = g_sht_back->bxsize;
	scrny = g_sht_back->bysize;

	taskbar_redraw((char *) g_buf_back, scrnx, scrny, start_hover, start_pressed);

	ctl = g_sht_back->ctl;
	n = taskbar_winlist_collect(ctl, sheets, TASKBAR_WIN_BTN_MAX);
	if (ctl != 0 && ctl->top >= 1) {
		struct SHEET *top_app = 0;
		for (i = ctl->top - 1; i >= 1; i--) {
			struct SHEET *s = ctl->sheets[i];
			if (taskbar_winlist_eligible(s)) {
				top_app = s;
				break;
			}
		}
		key_win_now = top_app;
	}

	n = taskbar_winlist_layout(scrnx, n, &btn_w);

	prev_clear_x0 = TASKBAR_WIN_X0;
	x = TASKBAR_WIN_X0;
	g_taskbar_btn_count = n;
	for (i = 0; i < n; i++) {
		int x1 = x + btn_w - 1;
		int focused = (sheets[i] == key_win_now);
		int hover = (i == g_taskbar_btn_hover);
		int pressed = (i == g_taskbar_btn_pressed);
		taskbar_winlist_label(sheets[i], label, sizeof(label));
		taskbar_winlist_draw_button(g_buf_back, scrnx, scrny, x, x1, label,
				focused, hover, pressed);
		g_taskbar_btns[i].sht = sheets[i];
		g_taskbar_btns[i].x0 = x;
		g_taskbar_btns[i].x1 = x1;
		x = x1 + 1 + TASKBAR_WIN_BTN_GAP;
	}
	for (; i < TASKBAR_WIN_BTN_MAX; i++) {
		g_taskbar_btns[i].sht = 0;
		g_taskbar_btns[i].x0 = 0;
		g_taskbar_btns[i].x1 = 0;
	}
	{
		int tray_w = g_clock_show_seconds ? 68 : TASKBAR_TRAY_W;
		int tray_left = scrnx - TASKBAR_TRAY_R_PAD - tray_w;
		int clear_x1 = tray_left - 1;
		int clear_x0 = (n > 0) ? (g_taskbar_btns[n - 1].x1 + 1) : prev_clear_x0;
		if (clear_x0 <= clear_x1) {
			taskbar_winlist_clear(g_buf_back, scrnx, scrny, clear_x0, clear_x1);
		}
	}

	sheet_refresh(g_sht_back, 0, scrny - TASKBAR_HEIGHT, scrnx, scrny);
	bench_leave(BENCH_TASKBAR);
	return;
}

int taskbar_winlist_hit(int mx, int my)
{
	int i;
	int scrny;
	if (g_sht_back == 0) return -1;
	scrny = g_sht_back->bysize;
	if (my < scrny - TASKBAR_START_Y_PAD_TOP || my > scrny - TASKBAR_START_Y_PAD_BOT) {
		return -1;
	}
	for (i = 0; i < g_taskbar_btn_count; i++) {
		if (g_taskbar_btns[i].sht == 0) continue;
		if (g_taskbar_btns[i].x0 <= mx && mx <= g_taskbar_btns[i].x1) {
			return i;
		}
	}
	return -1;
}

struct SHEET *taskbar_winlist_sheet_at(int idx)
{
	if (idx < 0 || idx >= g_taskbar_btn_count) return 0;
	return g_taskbar_btns[idx].sht;
}

/* z-order 상위에서 아래로 내려가며 첫 번째 eligible app sheet 를 찾는다.
 * 닫힌 창 다음에 새 focus 를 정할 때 sht_mouse / sht_back / system widget 을
 * 건너뛰고 실제 app 윈도우를 골라준다. 후보가 없으면 0. */
static struct SHEET *find_topmost_app_sheet(struct SHTCTL *ctl)
{
	int i;
	if (ctl == 0) return 0;
	for (i = ctl->top - 1; i >= 1; i--) {
		struct SHEET *s = ctl->sheets[i];
		if (taskbar_winlist_eligible(s)) return s;
	}
	return 0;
}

void alt_tab_cycle(int prev)
{
	struct SHTCTL *ctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *list[MAX_SHEETS];
	int n = 0, i, focus_idx = -1, next_idx;
	struct SHEET *focus_now = 0;
	if (ctl == 0) return;
	/* z-order 상위에서 낮은 순으로 enumerate — 스택 순서. */
	for (i = ctl->top - 1; i >= 1; i--) {
		struct SHEET *s = ctl->sheets[i];
		if (taskbar_winlist_eligible(s)) {
			list[n++] = s;
			if (focus_now == 0) {
				focus_now = s;
				focus_idx = n - 1;
			}
		}
	}
	if (n < 1) return;
	if (n == 1) {
		/* 단 하나 있는 경우라도 focus 만 다시 set */
		system_request_keywin(list[0]);
		return;
	}
	if (focus_idx < 0) focus_idx = 0;
	if (prev) {
		next_idx = (focus_idx - 1 + n) % n;
	} else {
		next_idx = (focus_idx + 1) % n;
	}
	{
		struct SHEET *target = list[next_idx];
		int h = ctl->top;
		if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
			h = g_sht_mouse->height;
		}
		sheet_updown(target, h);
		if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
			if (g_sht_mouse->height != ctl->top) {
				sheet_updown(g_sht_mouse, ctl->top);
			}
			sheet_slide(g_sht_mouse, g_sht_mouse->vx0, g_sht_mouse->vy0);
		}
		system_request_keywin(target);
	}
	return;
}

static int system_modal_is_open(void)
{
	return g_run_dialog.sht != 0 || g_about_dialog.sht != 0;
}

static void system_draw_button(struct SYSTEM_DIALOG *dlg,
		int x0, int y0, int w, int h, char *label)
{
	boxfill8(dlg->buf, dlg->w, COL8_C6C6C6, x0, y0, x0 + w - 1, y0 + h - 1);
	boxfill8(dlg->buf, dlg->w, COL8_FFFFFF, x0, y0, x0 + w - 2, y0);
	boxfill8(dlg->buf, dlg->w, COL8_FFFFFF, x0, y0, x0, y0 + h - 2);
	boxfill8(dlg->buf, dlg->w, COL8_848484, x0 + 1, y0 + h - 2, x0 + w - 1, y0 + h - 2);
	boxfill8(dlg->buf, dlg->w, COL8_848484, x0 + w - 2, y0 + 1, x0 + w - 2, y0 + h - 1);
	boxfill8(dlg->buf, dlg->w, COL8_000000, x0, y0 + h - 1, x0 + w - 1, y0 + h - 1);
	boxfill8(dlg->buf, dlg->w, COL8_000000, x0 + w - 1, y0, x0 + w - 1, y0 + h - 1);
	putfonts8_asc(dlg->buf, dlg->w, x0 + (w - (int) strlen(label) * 8) / 2,
			y0 + 5, COL8_000000, (unsigned char *) label);
	return;
}

/* work5 Phase 7: 버튼이 keyboard focus 일 때 안쪽 1px 점선 테두리를 그린다.
 * 1px 점선은 짝수 픽셀만 검정으로 칠하는 패턴. */
static void system_draw_focus_ring(struct SYSTEM_DIALOG *dlg,
		int x0, int y0, int w, int h)
{
	int x, y;
	int rx0 = x0 + 3, rx1 = x0 + w - 4;
	int ry0 = y0 + 3, ry1 = y0 + h - 4;
	for (x = rx0; x <= rx1; x++) {
		if (((x - rx0) & 1) == 0) {
			dlg->buf[ry0 * dlg->w + x] = COL8_000000;
			dlg->buf[ry1 * dlg->w + x] = COL8_000000;
		}
	}
	for (y = ry0; y <= ry1; y++) {
		if (((y - ry0) & 1) == 0) {
			dlg->buf[y * dlg->w + rx0] = COL8_000000;
			dlg->buf[y * dlg->w + rx1] = COL8_000000;
		}
	}
	return;
}

static void system_dialog_close(struct SYSTEM_DIALOG *dlg)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	if (dlg->sht == 0) {
		return;
	}
	sheet_updown(dlg->sht, -1);
	memman_free_4k(memman, (int) dlg->buf, dlg->w * dlg->h);
	sheet_free(dlg->sht);
	dlg->sht = 0;
	dlg->buf = 0;
	dlg->len = 0;
	return;
}

static void system_run_redraw(void)
{
	struct SYSTEM_DIALOG *dlg = &g_run_dialog;
	if (dlg->sht == 0) {
		return;
	}
	make_window8(dlg->buf, dlg->w, dlg->h, "Run", 1);
	putfonts8_asc(dlg->buf, dlg->w, 18, 34, COL8_000000, (unsigned char *) "Open:");
	make_textbox8(dlg->sht, 62, 32, 240, 18, COL8_FFFFFF);
	putfonts8_asc(dlg->buf, dlg->w, 66, 34, COL8_000000, (unsigned char *) dlg->input);
	if (dlg->focus == 0) {
		boxfill8(dlg->buf, dlg->w, COL8_000000,
				66 + dlg->len * 8, 34, 66 + dlg->len * 8, 49);
	}
	system_draw_button(dlg, 170, 68, 60, 22, "OK");
	system_draw_button(dlg, 240, 68, 60, 22, "Cancel");
	if (dlg->focus == 1) {
		system_draw_focus_ring(dlg, 170, 68, 60, 22);
	} else if (dlg->focus == 2) {
		system_draw_focus_ring(dlg, 240, 68, 60, 22);
	}
	sheet_refresh(dlg->sht, 0, 0, dlg->w, dlg->h);
	return;
}

static void system_run_submit(void)
{
	if (g_run_dialog.len > 0) {
		system_start_command(g_run_dialog.input, g_memtotal);
	}
	system_dialog_close(&g_run_dialog);
	return;
}

static void system_run_open(void)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	if (g_run_dialog.sht != 0) {
		system_raise_sheet(g_run_dialog.sht);
		return;
	}
	start_menu_close_all();
	g_run_dialog.w = RUN_DIALOG_W;
	g_run_dialog.h = RUN_DIALOG_H;
	g_run_dialog.len = 0;
	g_run_dialog.input[0] = 0;
	g_run_dialog.focus = 0;
	g_run_dialog.sht = sheet_alloc(shtctl);
	g_run_dialog.buf = (unsigned char *) memman_alloc_4k(memman, RUN_DIALOG_W * RUN_DIALOG_H);
	sheet_setbuf(g_run_dialog.sht, g_run_dialog.buf, RUN_DIALOG_W, RUN_DIALOG_H, -1);
	g_run_dialog.sht->flags &= ~SHEET_FLAG_RESIZABLE;
	g_run_dialog.sht->flags |= SHEET_FLAG_SYSTEM_WIDGET;
	system_run_redraw();
	sheet_slide(g_run_dialog.sht, (shtctl->xsize - RUN_DIALOG_W) / 2,
			(shtctl->ysize - TASKBAR_HEIGHT - RUN_DIALOG_H) / 2);
	system_raise_sheet(g_run_dialog.sht);
	return;
}

static void system_about_open(void)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct TASK *task = task_now();
	char s[40];
	if (g_about_dialog.sht != 0) {
		system_raise_sheet(g_about_dialog.sht);
		return;
	}
	start_menu_close_all();
	g_about_dialog.w = ABOUT_DIALOG_W;
	g_about_dialog.h = ABOUT_DIALOG_H;
	g_about_dialog.sht = sheet_alloc(shtctl);
	g_about_dialog.buf = (unsigned char *) memman_alloc_4k(memman, ABOUT_DIALOG_W * ABOUT_DIALOG_H);
	sheet_setbuf(g_about_dialog.sht, g_about_dialog.buf, ABOUT_DIALOG_W, ABOUT_DIALOG_H, -1);
	g_about_dialog.sht->flags &= ~SHEET_FLAG_RESIZABLE;
	g_about_dialog.sht->flags |= SHEET_FLAG_SYSTEM_WIDGET;
	make_window8(g_about_dialog.buf, ABOUT_DIALOG_W, ABOUT_DIALOG_H, "About BxOS", 1);
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 34, COL8_000000, (unsigned char *) "BxOS");
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 50, COL8_000000, (unsigned char *) "work5 Phase 4");
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 66, COL8_000000, (unsigned char *) __DATE__);
	sprintf(s, "Screen: %dx%d", shtctl->xsize, shtctl->ysize);
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 82, COL8_000000, (unsigned char *) s);
	sprintf(s, "Memory: %uMB", g_memtotal / (1024 * 1024));
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 98, COL8_000000, (unsigned char *) s);
	sprintf(s, "Langmode: %d", task != 0 ? task->langmode : 0);
	putfonts8_asc(g_about_dialog.buf, ABOUT_DIALOG_W, 18, 114, COL8_000000, (unsigned char *) s);
	system_draw_button(&g_about_dialog, 110, 132, 60, 22, "OK");
	sheet_slide(g_about_dialog.sht, (shtctl->xsize - ABOUT_DIALOG_W) / 2,
			(shtctl->ysize - TASKBAR_HEIGHT - ABOUT_DIALOG_H) / 2);
	system_raise_sheet(g_about_dialog.sht);
	return;
}

static int system_modal_handle_key(int key, int chr)
{
	if (g_about_dialog.sht != 0) {
		if (key == 0x01 || key == 0x1c) {
			system_dialog_close(&g_about_dialog);
		}
		return 1;
	}
	if (g_run_dialog.sht != 0) {
		if (key == 0x01) {
			system_dialog_close(&g_run_dialog);
		} else if (key == 0x0f) {
			/* work5 Phase 7: Tab → focus 순환 input → OK → Cancel → input */
			g_run_dialog.focus = (g_run_dialog.focus + 1) % 3;
			system_run_redraw();
		} else if (key == 0x1c) {
			/* Enter — focus 위치에 따라 동작이 다르다 */
			if (g_run_dialog.focus == 2) {
				system_dialog_close(&g_run_dialog);
			} else {
				system_run_submit();
			}
		} else if (g_run_dialog.focus == 0 && chr == 0x08) {
			if (g_run_dialog.len > 0) {
				g_run_dialog.input[--g_run_dialog.len] = 0;
				system_run_redraw();
			}
		} else if (g_run_dialog.focus == 0 &&
				32 <= chr && chr <= 126 && g_run_dialog.len < CONS_CMDLINE_MAX - 1) {
			g_run_dialog.input[g_run_dialog.len++] = chr;
			g_run_dialog.input[g_run_dialog.len] = 0;
			system_run_redraw();
		} else if (g_run_dialog.focus != 0 && chr == ' ') {
			/* Space on a focused button activates it (Win95-style). */
			if (g_run_dialog.focus == 1) {
				system_run_submit();
			} else {
				system_dialog_close(&g_run_dialog);
			}
		}
		return 1;
	}
	return 0;
}

static int system_modal_handle_mouse(int mx, int my, int btn, int old_btn)
{
	struct SYSTEM_DIALOG *dlg = 0;
	int x, y, btn_up = (old_btn & ~btn) & 0x01;
	if (g_about_dialog.sht != 0) {
		dlg = &g_about_dialog;
	} else if (g_run_dialog.sht != 0) {
		dlg = &g_run_dialog;
	}
	if (dlg == 0) {
		return 0;
	}
	x = mx - dlg->sht->vx0;
	y = my - dlg->sht->vy0;
	if (btn_up != 0 && 0 <= x && x < dlg->w && 0 <= y && y < dlg->h) {
		if (dlg == &g_run_dialog) {
			if (170 <= x && x < 230 && 68 <= y && y < 90) {
				system_run_submit();
			} else if ((240 <= x && x < 300 && 68 <= y && y < 90) ||
					(dlg->w - 21 <= x && x < dlg->w - 5 && 5 <= y && y < 19)) {
				system_dialog_close(dlg);
			}
		} else {
			if ((110 <= x && x < 170 && 132 <= y && y < 154) ||
					(dlg->w - 21 <= x && x < dlg->w - 5 && 5 <= y && y < 19)) {
				system_dialog_close(dlg);
			}
		}
	}
	return 1;
}

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

static void system_shutdown(void)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct SYSTEM_DIALOG dlg;

	dlg.w = 300;
	dlg.h = 80;
	dlg.sht = sheet_alloc(shtctl);
	dlg.buf = (unsigned char *) memman_alloc_4k(memman, dlg.w * dlg.h);
	sheet_setbuf(dlg.sht, dlg.buf, dlg.w, dlg.h, -1);
	dlg.sht->flags &= ~SHEET_FLAG_RESIZABLE;
	dlg.sht->flags |= SHEET_FLAG_SYSTEM_WIDGET;
	make_window8(dlg.buf, dlg.w, dlg.h, "Shutdown", 1);
	putfonts8_asc(dlg.buf, dlg.w, 18, 36, COL8_000000,
			(unsigned char *) "It is now safe to power off.");
	sheet_slide(dlg.sht, (shtctl->xsize - dlg.w) / 2,
			(shtctl->ysize - TASKBAR_HEIGHT - dlg.h) / 2);
	system_raise_sheet(dlg.sht);
	io_cli();
	for (;;) {
		io_hlt();
	}
}

void start_menu_dispatch(struct MENU_ITEM *item)
{
	struct SHTCTL *shtctl = (struct SHTCTL *) *((int *) 0x0fe4);
	struct SHEET *sht;
	char cmd[CONS_CMDLINE_MAX];
	int i, j;

	if (item == 0) {
		return;
	}
	taskbar_mark_dirty();
	if (item->handler_id == KMENU_HANDLER_EXEC) {
		system_start_command(item->arg, g_memtotal);
		return;
	}
	if (item->handler_id == KMENU_HANDLER_SETTINGS) {
		strcpy(cmd, "/SETTINGS.HE2 ");
		for (i = 0, j = strlen(cmd); item->arg[i] != 0 && j < CONS_CMDLINE_MAX - 1; i++, j++) {
			cmd[j] = item->arg[i];
		}
		cmd[j] = 0;
		system_start_command(cmd, g_memtotal);
		return;
	}
	if (item->handler_id != KMENU_HANDLER_BUILTIN) {
		return;
	}
	if (strcmp(item->arg, "console") == 0) {
		sht = open_console(shtctl, g_memtotal);
		sheet_slide(sht, 32, 4);
		sheet_updown(sht, shtctl->top);
		system_request_keywin(sht);
	} else if (strcmp(item->arg, "taskmgr") == 0) {
		open_taskmgr(g_memtotal);
	} else if (strcmp(item->arg, "run") == 0) {
		system_run_open();
	} else if (strcmp(item->arg, "about") == 0) {
		system_about_open();
	} else if (strcmp(item->arg, "settings") == 0) {
		system_start_command("/SETTINGS.HE2", g_memtotal);
	} else if (strcmp(item->arg, "debug") == 0) {
		dbg_open();
	} else if (strcmp(item->arg, "shutdown") == 0) {
		system_shutdown();
	} else if (strcmp(item->arg, "restart") == 0) {
		wait_KBC_sendready();
		io_out8(PORT_KEYCMD, 0xfe);
		for (;;) {
			io_hlt();
		}
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
	struct TIMER *clock_timer;
	unsigned char *buf_back, buf_mouse[SIZE_MOUSE_CURSOR*MAX_MOUSE_CURSOR];
	struct SHEET *sht_back, *sht_mouse;
	struct TASK *task_a, *task;
	static char keytable0[0x80] = {
		0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x08, 0x09, /* 0x0f=Tab */
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', 0x0a,   0,   'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`',   0,   '\\', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', ',', '.', '/', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
	};
	static char keytable1[0x80] = {
		0,   0,   '!', '@', '#', '$', '%', '&', '^', '*', '(', ')', '_', '+', 0x08, 0x09, /* 0x0f=Shift+Tab */
		'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0x0a,   0,   'A', 'S',
		'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,   '|', 'Z', 'X', 'C', 'V',
		'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ', 0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1',
		'2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
		0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
	};
	int key_shift = 0, key_ctrl = 0, key_alt = 0;
	int key_leds = (binfo->leds >> 4) & 7, keycmd_wait = -1, key_e0 = 0;
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
	int start_hover = 0, start_pressed = 0;
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
	system_settings_load_boot();

	bench_init();
	init_palette();
	shtctl = shtctl_init(memman, binfo->vram, binfo->scrnx, binfo->scrny);
	task_a = task_init(memman);
	fifo.task = task_a;
	task_run(task_a, 1, 2);
	*((int *) 0x0fe4) = (int) shtctl;
	task_a->langmode = g_default_langmode;

	/* sht_back */
	sht_back  = sheet_alloc(shtctl);
	buf_back  = (unsigned char *) memman_alloc_4k(memman, binfo->scrnx * binfo->scrny);
	sheet_setbuf(sht_back, buf_back, binfo->scrnx, binfo->scrny, -1); /* 투명색없음 */
	g_sht_back = sht_back;
	g_buf_back = buf_back;
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

	/* work5 Phase 6: 부팅 시에는 console / debug 둘 다 자동 실행하지 않는다.
	 * 사용자가 Start Menu / Run / 단축키로 직접 띄운다. */
	key_win = 0;

	/* sht_mouse */
	sht_mouse = sheet_alloc(shtctl);
	sheet_setbuf(sht_mouse, buf_mouse, 16, 16, 99);
	init_mouse_cursor8(buf_mouse, 99);
	g_sht_mouse = sht_mouse;
	g_mouse_buf = buf_mouse;
	g_mouse_cursor = BX_CURSOR_ARROW;
	start_menu_init(shtctl, memman, binfo->scrnx, binfo->scrny);
	mx = (binfo->scrnx - 16) / 2; /* 화면 중앙이 되도록 좌표 계산 */
	my = (binfo->scrny - 28 - 16) / 2;

	sheet_slide(sht_back,  0,  0);
	sheet_slide(sht_mouse, mx, my);
	sheet_updown(sht_back,  0);
	sheet_updown(sht_mouse, 1);

	/* 처음에 키보드 상태와 어긋나지 않게, 설정해 두기로 한다 */
	fifo32_put(&keycmd, KEYCMD_LED);
	fifo32_put(&keycmd, key_leds);
	clock_timer = timer_alloc();
	timer_init(clock_timer, &fifo, TIMER_CLOCK);
	timer_settime(clock_timer, g_clock_show_seconds ? 100 : 60 * 100);

	/* debug window 도 부팅 시점에는 hidden. dbg_init 으로 sheet/scrollwin 만
	 * alloc 해 두고 (다른 코드가 dbg_get() 를 참조하므로 alloc 자체는 필요),
	 * 첫 표시는 Start → Debug 메뉴 선택 시 dbg_open() 으로 한다. */
	dbg_init(shtctl);

	/* taskbar 의 윈도우 목록 buttons 을 부팅 직후 한 번 그려둔다 (현재는 0개). */
	taskbar_full_redraw(0, 0);

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
				key_win = find_topmost_app_sheet(shtctl);
				if (key_win != 0) {
					keywin_on(key_win);
				}
				taskbar_mark_dirty();
			}
			if (256 <= i && i <= 511) { /* 키보드 데이터 */
				/*
					keyboard code : http://ubuntuforums.org/archive/index.php/t-1059755.html
				*/
				if (i == 256 + 0xe0) {
					key_e0 = 1;
				} else if (key_e0 != 0) {
					key_e0 = 0;
					if (start_menu_is_open() && start_menu_handle_key(i - 256) != 0) {
						taskbar_full_redraw(start_hover, start_pressed);
					} else if (key_win != 0 && key_win->task != 0) {
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
					int key_menu_consumed = 0;
					if (i == 256 + 0x1d) {	/* Ctrl ON */
						key_ctrl |= 1;
					}
					if (i == 256 + 0x9d) {	/* Ctrl OFF */
						key_ctrl &= ~1;
					}
					if (i == 256 + 0x38) {	/* Alt ON */
						key_alt |= 1;
					}
					if (i == 256 + 0xb8) {	/* Alt OFF */
						key_alt &= ~1;
					}
					if (i == 256 + 0x01 && key_ctrl != 0 &&
							!system_modal_is_open()) {	/* Ctrl+Esc */
						start_menu_toggle();
						start_pressed = 0;
						taskbar_full_redraw(start_hover, start_pressed);
						key_menu_consumed = 1;
					} else if (i == 256 + 0x13 && key_ctrl != 0 &&
							!start_menu_is_open() && !system_modal_is_open()) {	/* Ctrl+R */
						system_run_open();
						key_menu_consumed = 1;
					} else if (i == 256 + 0x0f && key_alt != 0 &&
							!start_menu_is_open() && !system_modal_is_open()) {
						/* work5 Phase 6: Alt+Tab / Alt+Shift+Tab → window-cycle */
						alt_tab_cycle(key_shift != 0);
						key_menu_consumed = 1;
					} else if (start_menu_is_open() && start_menu_handle_key(i - 256) != 0) {
						taskbar_full_redraw(start_hover, start_pressed);
						key_menu_consumed = 1;
					}
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
					if (key_menu_consumed == 0 &&
							system_modal_handle_key(i - 256, s[0]) != 0) {
						key_menu_consumed = 1;
					}
					/* work5 Phase 7: 메뉴 열린 동안 ANY 키 leak 방지.
					 * char 가 hotkey 와 매칭되면 항목 invoke. 아니면 그냥 consume. */
					if (key_menu_consumed == 0 && start_menu_is_open()) {
						if (s[0] != 0) {
							start_menu_handle_char(s[0]);
						}
						key_menu_consumed = 1;
						taskbar_full_redraw(start_hover, start_pressed);
					}
					if (key_menu_consumed != 0) {
						s[0] = 0;
					}
					if (s[0] != 0 && key_win != 0 && key_win->task != 0) { /* 통상 문자, 백 스페이스, Enter */
						fifo32_put(&key_win->task->fifo, s[0] + 256);
					}
					/* work5 Phase 6: 기존 Tab → window z-order swap 은 제거.
					 * window 순환은 Alt+Tab 으로 옮겼고(위), 단독 Tab 은 char(0x09)
					 * 로 focused app 에 그대로 전달된다(explorer tree↔list 등).      */
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
					if (key_menu_consumed == 0 && i == 256 + 0x3b &&
							key_shift != 0 && key_win != 0 && key_win->task != 0) {	/* Shift+F1 */
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
					if (key_menu_consumed == 0 && i == 256 + 0x3c && key_shift != 0) {	/* Shift+F2 */
						/* 새롭게 만든 콘솔을 입력 선택 상태로 한다(그 편이 친절하겠지요? ) */
						if (key_win != 0) {
							keywin_off(key_win);
						}
						key_win = open_console(shtctl, memtotal);
						sheet_slide(key_win, 32, 4);
						sheet_updown(key_win, shtctl->top);
						keywin_on(key_win);
						taskbar_mark_dirty();
					}
					if (key_menu_consumed == 0 && i == 256 + 0x57) {	/* F11 */
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
					int taskbar_mouse_consumed = 0;
					int menu_mouse_consumed = 0;
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
					{
						int next_start_hover =
							(TASKBAR_START_X0 <= mx && mx <= TASKBAR_START_X1 &&
							 binfo->scrny - TASKBAR_START_Y_PAD_TOP <= my &&
							 my <= binfo->scrny - TASKBAR_START_Y_PAD_BOT);
						int next_winlist_hover = taskbar_winlist_hit(mx, my);
						int dirty = 0;
						if (next_start_hover != start_hover) {
							start_hover = next_start_hover;
							dirty = 1;
						}
						if (next_winlist_hover != g_taskbar_btn_hover) {
							g_taskbar_btn_hover = next_winlist_hover;
							dirty = 1;
						}
						if (dirty) {
							taskbar_full_redraw(start_hover, start_pressed);
						}
					}
					if (system_modal_is_open()) {
						menu_mouse_consumed = system_modal_handle_mouse(mx, my, mdec.btn, old_btn);
						taskbar_mouse_consumed = menu_mouse_consumed;
					} else if (start_menu_is_open()) {
						menu_mouse_consumed = start_menu_handle_mouse(mx, my, mdec.btn, old_btn);
						if (!start_menu_is_open()) {
							start_pressed = 0;
							taskbar_full_redraw(start_hover, start_pressed);
						}
					}

					/* ── (a) 스크롤바가 이미 잡혀있다면 해당 텍스트 창으로 우회. */
					if (menu_mouse_consumed != 0) {
						taskbar_mouse_consumed = 1;
					} else if (dbg_get()->sw.sb_grab) {
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
						} else if (start_pressed != 0 || start_hover != 0) {
							taskbar_mouse_consumed = 1;
							if (start_pressed == 0) {
								start_pressed = 1;
								taskbar_full_redraw(start_hover, start_pressed);
							}
						} else if (g_taskbar_btn_pressed >= 0 || g_taskbar_btn_hover >= 0) {
							/* work5 Phase 6: 윈도우 목록 버튼 누름 (release 시 activate) */
							taskbar_mouse_consumed = 1;
							if (g_taskbar_btn_pressed != g_taskbar_btn_hover) {
								g_taskbar_btn_pressed = g_taskbar_btn_hover;
								taskbar_full_redraw(start_hover, start_pressed);
							}
						} else if (mmx < 0) {
							/* 통상 모드의 경우 */
							/* 위 레이어부터 차례로 마우스가 가리키고 있는 레이어를 찾는다 */
							for (j = shtctl->top - 1; j > 0; j--) {
								sht = shtctl->sheets[j];
								if ((sht->flags & SHEET_FLAG_SYSTEM_WIDGET) != 0) {
									continue;
								}
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
										taskbar_mark_dirty();
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
												key_win = find_topmost_app_sheet(shtctl);
												keywin_on(key_win);
												io_cli();
												fifo32_put(&task->fifo, 4);
												io_sti();
												taskbar_mark_dirty();
											} else {	/* 태스크 없는 도구 창 */
												sheet_updown(sht, -1);
												if (sht == key_win) {
													key_win = find_topmost_app_sheet(shtctl);
													keywin_on(key_win);
												}
												taskbar_mark_dirty();
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
						if (start_pressed != 0) {
							if (start_hover != 0) {
								start_menu_toggle();
							}
							start_pressed = 0;
							taskbar_mouse_consumed = 1;
							taskbar_full_redraw(start_hover, start_pressed);
						}
						if (g_taskbar_btn_pressed >= 0) {
							/* work5 Phase 6: 윈도우 목록 버튼 release → activate */
							int idx = g_taskbar_btn_pressed;
							struct SHEET *target = (idx == g_taskbar_btn_hover)
									? taskbar_winlist_sheet_at(idx) : 0;
							g_taskbar_btn_pressed = -1;
							taskbar_mouse_consumed = 1;
							if (target != 0) {
								int h = shtctl->top;
								if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
									h = g_sht_mouse->height;
								}
								if (target == key_win) {
									/* 이미 focus 인 버튼: 최소화 대신 z-order 만 유지 (Phase 6 1차) */
								} else {
									sheet_updown(target, h);
									if (g_sht_mouse != 0 &&
											g_sht_mouse->height >= 0 &&
											g_sht_mouse->height != shtctl->top) {
										sheet_updown(g_sht_mouse, shtctl->top);
									}
									keywin_off(key_win);
									key_win = target;
									keywin_on(key_win);
								}
							}
							taskbar_full_redraw(start_hover, start_pressed);
						}
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
							taskbar_mouse_consumed == 0 &&
							!dbg_get()->sw.sb_grab &&
							!(key_win != 0 && key_win->scroll != 0 &&
									key_win->scroll->sb_grab)) {
						struct SHEET *app_sht = 0;
						int app_x = 0, app_y = 0;
						int sj, sx, sy;
						for (sj = shtctl->top - 1; sj > 0; sj--) {
							struct SHEET *s = shtctl->sheets[sj];
							if ((s->flags & SHEET_FLAG_SYSTEM_WIDGET) != 0) {
								continue;
							}
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
								if ((s->flags & SHEET_FLAG_SYSTEM_WIDGET) != 0) {
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
				taskbar_mark_dirty();
			} else if (i == TIMER_CLOCK) {
				if (g_clock_show_seconds) {
					g_clock_seconds = (g_clock_seconds + 1) % (24 * 60 * 60);
					timer_settime(clock_timer, 100);
				} else {
					g_clock_seconds = (g_clock_seconds + 60) % (24 * 60 * 60);
					timer_settime(clock_timer, 60 * 100);
				}
				taskbar_full_redraw(start_hover, start_pressed);
			}
			if (768 <= i && i <= 2279) {
				/* console / app / taskmgr close 등 윈도우 목록 변동 가능 — 다시 그림 */
				taskbar_mark_dirty();
			}
			if (g_pending_key_win != 0) {
				if (key_win != 0) {
					keywin_off(key_win);
				}
				key_win = g_pending_key_win;
				g_pending_key_win = 0;
				keywin_on(key_win);
				taskbar_mark_dirty();
			}
			if (g_taskbar_dirty) {
				taskbar_full_redraw(start_hover, start_pressed);
				g_taskbar_dirty = 0;
			}
		}
	}
}

void keywin_off(struct SHEET *key_win)
{
	if (key_win == 0) return;
	change_wtitle8(key_win, 0);
	if ((key_win->flags & 0x20) != 0 && key_win->task != 0) {
		fifo32_put(&key_win->task->fifo, 3); /* 콘솔의 커서 OFF */
	}
	return;
}

void keywin_on(struct SHEET *key_win)
{
	if (key_win == 0) return;
	change_wtitle8(key_win, 1);
	if ((key_win->flags & 0x20) != 0 && key_win->task != 0) {
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
