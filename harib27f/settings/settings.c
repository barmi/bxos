/* settings.c — work5 Phase 5 Settings app. */

#include "apilib.h"
#include <string.h>

#define WIN_W		430
#define WIN_H		280
#define FRAME_L		3
#define FRAME_T		21
#define SIDE_W		116
#define ROW_H		24
#define SETTINGS_PATH	"/SYSTEM/SETTINGS.CFG"

#define COL_BLACK	0
#define COL_BLUE	4
#define COL_WHITE	7
#define COL_LGRAY	8
#define COL_GREEN	10
#define COL_NAVY	12
#define COL_DGRAY	15

enum { SET_TYPE_ENUM, SET_TYPE_BOOL, SET_TYPE_INT, SET_TYPE_STR, SET_TYPE_ACTION };

struct SettingChoice {
	const char *value;
	const char *label;
};

struct SettingSpec {
	const char *category;
	const char *key;
	int type;
	const char *label;
	const char *help;
	const struct SettingChoice *choices;
	int n_choices;
	int int_min, int_max;
	const char *default_value;
	int restart_required;
};

struct Category {
	const char *id;
	const char *label;
};

static const struct Category g_categories[] = {
	{ "display",  "Display"  },
	{ "language", "Language" },
	{ "time",     "Time"     },
	{ "about",    "About"    }
};

static const struct SettingChoice g_bg_choices[] = {
	{ "navy",  "Navy"  },
	{ "black", "Black" },
	{ "gray",  "Gray"  },
	{ "green", "Green" }
};

static const struct SettingChoice g_lang_choices[] = {
	{ "0", "ASCII"  },
	{ "1", "EUC-JP" },
	{ "2", "SJIS"   },
	{ "3", "EUC-KR" },
	{ "4", "UTF-8"  }
};

static const struct SettingSpec g_settings[] = {
	{ "display",  "display.background",   SET_TYPE_ENUM,   "Background color",
	  "Applied after restart.", g_bg_choices, 4, 0, 0, "navy", 1 },
	{ "language", "language.mode",        SET_TYPE_ENUM,   "Language mode",
	  "New consoles use this after restart.", g_lang_choices, 5, 0, 0, "0", 1 },
	{ "time",     "time.boot_offset_min", SET_TYPE_INT,    "Clock offset min",
	  "Boot clock starts from this minute.", 0, 0, 0, 1439, "0", 1 },
	{ "time",     "time.show_seconds",    SET_TYPE_BOOL,   "Show seconds",
	  "Tray clock format.", 0, 0, 0, 0, "false", 1 },
	{ "about",    "about",                SET_TYPE_ACTION, "About BxOS",
	  "Use Start > About BxOS for system info.", 0, 0, 0, 0, "", 0 }
};

#define N_CATEGORIES	((int)(sizeof g_categories / sizeof g_categories[0]))
#define N_SETTINGS	((int)(sizeof g_settings / sizeof g_settings[0]))

static char *G_buf;
static int G_win;
static int G_cat;
static int G_sel;
static char G_values[N_SETTINGS][32];
static char G_status[64];

static int streq(const char *a, const char *b)
{
	int i;
	for (i = 0; a[i] != 0 || b[i] != 0; i++) {
		if (a[i] != b[i]) return 0;
	}
	return 1;
}

static void scpy(char *dst, const char *src, int max)
{
	int i;
	for (i = 0; i < max - 1 && src[i] != 0; i++) dst[i] = src[i];
	dst[i] = 0;
}

static int slen(const char *s)
{
	int n = 0;
	while (s[n] != 0) n++;
	return n;
}

static char *trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	e = s + slen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
		*--e = 0;
	}
	return s;
}

static int s_x(int x) { return x + FRAME_L; }
static int s_y(int y) { return y + FRAME_T; }

static void box(int x0, int y0, int x1, int y1, int col)
{
	api_boxfilwin(G_win | 1, s_x(x0), s_y(y0), s_x(x1), s_y(y1), col);
}

static void line(int x0, int y0, int x1, int y1, int col)
{
	api_linewin(G_win | 1, s_x(x0), s_y(y0), s_x(x1), s_y(y1), col);
}

static void text(int x, int y, int col, const char *s)
{
	api_putstrwin(G_win | 1, s_x(x), s_y(y), col, slen(s), (char *) s);
}

static void draw_button(int x, int y, int w, int h, const char *label)
{
	box(x, y, x + w - 1, y + h - 1, COL_LGRAY);
	line(x, y, x + w - 2, y, COL_WHITE);
	line(x, y, x, y + h - 2, COL_WHITE);
	line(x + 1, y + h - 2, x + w - 1, y + h - 2, COL_DGRAY);
	line(x + w - 2, y + 1, x + w - 2, y + h - 1, COL_DGRAY);
	line(x, y + h - 1, x + w - 1, y + h - 1, COL_BLACK);
	line(x + w - 1, y, x + w - 1, y + h - 1, COL_BLACK);
	text(x + (w - slen(label) * 8) / 2, y + 5, COL_BLACK, label);
}

static int setting_index_at(int ordinal)
{
	int i, n = 0;
	for (i = 0; i < N_SETTINGS; i++) {
		if (streq(g_settings[i].category, g_categories[G_cat].id)) {
			if (n == ordinal) return i;
			n++;
		}
	}
	return -1;
}

static int setting_count_in_cat(void)
{
	int i, n = 0;
	for (i = 0; i < N_SETTINGS; i++) {
		if (streq(g_settings[i].category, g_categories[G_cat].id)) n++;
	}
	return n;
}

static int value_choice_index(int idx)
{
	int i;
	for (i = 0; i < g_settings[idx].n_choices; i++) {
		if (streq(G_values[idx], g_settings[idx].choices[i].value)) return i;
	}
	return 0;
}

static int value_int(int idx)
{
	int i, v = 0;
	for (i = 0; G_values[idx][i] >= '0' && G_values[idx][i] <= '9'; i++) {
		v = v * 10 + G_values[idx][i] - '0';
	}
	return v;
}

static void itoa_dec(int v, char *out)
{
	char tmp[12];
	int i = 0, j = 0;
	if (v == 0) {
		out[0] = '0';
		out[1] = 0;
		return;
	}
	while (v > 0 && i < 10) {
		tmp[i++] = '0' + (v % 10);
		v /= 10;
	}
	while (i > 0) out[j++] = tmp[--i];
	out[j] = 0;
}

static void defaults_load(void)
{
	int i;
	for (i = 0; i < N_SETTINGS; i++) {
		scpy(G_values[i], g_settings[i].default_value, sizeof G_values[i]);
	}
	scpy(G_status, "Ready", sizeof G_status);
}

static int find_setting_by_key(char *key)
{
	int i;
	for (i = 0; i < N_SETTINGS; i++) {
		if (streq(key, g_settings[i].key)) return i;
	}
	return -1;
}

static void settings_load(void)
{
	int h, size, n, idx;
	char *buf, *p, *linep, *eq, *end, *key, *value;
	defaults_load();
	h = api_fopen(SETTINGS_PATH);
	if (h == 0) return;
	size = api_fsize(h, 0);
	if (size <= 0 || size > 2048) {
		api_fclose(h);
		return;
	}
	buf = api_malloc(size + 1);
	if (buf == 0) {
		api_fclose(h);
		return;
	}
	n = api_fread(buf, size, h);
	api_fclose(h);
	if (n < 0) n = 0;
	buf[n] = 0;
	p = buf;
	while (*p != 0) {
		linep = p;
		while (*p != 0 && *p != '\n') p++;
		if (*p == '\n') *p++ = 0;
		for (end = linep; *end != 0; end++) {
			if (*end == '#' || *end == ';') {
				*end = 0;
				break;
			}
		}
		linep = trim(linep);
		if (linep[0] == 0) continue;
		eq = linep;
		while (*eq != 0 && *eq != '=') eq++;
		if (*eq != '=') continue;
		*eq = 0;
		key = trim(linep);
		value = trim(eq + 1);
		idx = find_setting_by_key(key);
		if (idx >= 0) scpy(G_values[idx], value, sizeof G_values[idx]);
	}
	api_free(buf, size + 1);
}

static void append(char *buf, int *pos, int max, const char *s)
{
	int i;
	for (i = 0; s[i] != 0 && *pos < max - 1; i++) {
		buf[(*pos)++] = s[i];
	}
	buf[*pos] = 0;
}

static void settings_save(void)
{
	char out[512];
	int h, i, pos = 0, written;
	append(out, &pos, sizeof out, "# /SYSTEM/SETTINGS.CFG -- saved by Settings.\n");
	for (i = 0; i < N_SETTINGS; i++) {
		if (g_settings[i].type == SET_TYPE_ACTION) continue;
		append(out, &pos, sizeof out, g_settings[i].key);
		append(out, &pos, sizeof out, " = ");
		append(out, &pos, sizeof out, G_values[i]);
		append(out, &pos, sizeof out, "\n");
	}
	h = api_fopen_w(SETTINGS_PATH);
	if (h == 0) {
		scpy(G_status, "save failed", sizeof G_status);
		return;
	}
	written = api_fwrite(out, pos, h);
	api_fclose(h);
	if (written == pos) {
		scpy(G_status, "Saved. Restart applies system changes.", sizeof G_status);
	} else {
		scpy(G_status, "save failed", sizeof G_status);
	}
}

static void change_setting(int idx, int delta)
{
	int c, v;
	if (idx < 0) return;
	if (g_settings[idx].type == SET_TYPE_ENUM) {
		c = value_choice_index(idx) + delta;
		if (c < 0) c = g_settings[idx].n_choices - 1;
		if (c >= g_settings[idx].n_choices) c = 0;
		scpy(G_values[idx], g_settings[idx].choices[c].value, sizeof G_values[idx]);
		settings_save();
	} else if (g_settings[idx].type == SET_TYPE_BOOL) {
		scpy(G_values[idx], streq(G_values[idx], "true") ? "false" : "true",
				sizeof G_values[idx]);
		settings_save();
	} else if (g_settings[idx].type == SET_TYPE_INT) {
		v = value_int(idx) + delta;
		if (v < g_settings[idx].int_min) v = g_settings[idx].int_min;
		if (v > g_settings[idx].int_max) v = g_settings[idx].int_max;
		itoa_dec(v, G_values[idx]);
		settings_save();
	} else if (g_settings[idx].type == SET_TYPE_ACTION) {
		scpy(G_status, g_settings[idx].help, sizeof G_status);
	}
}

static void draw_settings(void)
{
	int i, row, idx, x, y, c, checked, n;
	char num[16];

	box(0, 0, WIN_W - 7, WIN_H - 25, COL_LGRAY);
	box(0, 0, SIDE_W - 1, WIN_H - 25, COL_DGRAY);
	box(1, 1, SIDE_W - 3, WIN_H - 27, COL_LGRAY);
	text(12, 8, COL_BLACK, "Settings");

	for (i = 0; i < N_CATEGORIES; i++) {
		y = 34 + i * ROW_H;
		if (i == G_cat) {
			box(8, y - 3, SIDE_W - 10, y + 17, COL_BLUE);
			text(16, y, COL_WHITE, g_categories[i].label);
		} else {
			text(16, y, COL_BLACK, g_categories[i].label);
		}
	}

	box(SIDE_W, 0, WIN_W - 7, WIN_H - 25, COL_WHITE);
	text(SIDE_W + 18, 12, COL_BLACK, g_categories[G_cat].label);
	line(SIDE_W + 18, 31, WIN_W - 26, 31, COL_DGRAY);
	line(SIDE_W + 18, 32, WIN_W - 26, 32, COL_LGRAY);

	n = setting_count_in_cat();
	if (G_sel >= n) G_sel = n - 1;
	if (G_sel < 0) G_sel = 0;

	row = 0;
	for (i = 0; i < N_SETTINGS; i++) {
		if (!streq(g_settings[i].category, g_categories[G_cat].id)) continue;
		idx = i;
		x = SIDE_W + 18;
		y = 46 + row * 48;
		if (row == G_sel) {
			box(x - 6, y - 4, WIN_W - 24, y + 38, COL_LGRAY);
		}
		text(x, y, COL_BLACK, g_settings[idx].label);
		if (g_settings[idx].restart_required) {
			text(WIN_W - 92, y, COL_DGRAY, "restart");
		}
		if (g_settings[idx].type == SET_TYPE_ENUM) {
			for (c = 0; c < g_settings[idx].n_choices; c++) {
				int ox = x + c * 58;
				checked = streq(G_values[idx], g_settings[idx].choices[c].value);
				line(ox, y + 22, ox + 8, y + 22, COL_BLACK);
				line(ox, y + 22, ox, y + 30, COL_BLACK);
				line(ox, y + 30, ox + 8, y + 30, COL_BLACK);
				line(ox + 8, y + 22, ox + 8, y + 30, COL_BLACK);
				if (checked) box(ox + 3, y + 25, ox + 5, y + 27, COL_BLACK);
				text(ox + 14, y + 19, COL_BLACK, g_settings[idx].choices[c].label);
			}
		} else if (g_settings[idx].type == SET_TYPE_BOOL) {
			line(x, y + 22, x + 10, y + 22, COL_BLACK);
			line(x, y + 22, x, y + 32, COL_BLACK);
			line(x, y + 32, x + 10, y + 32, COL_BLACK);
			line(x + 10, y + 22, x + 10, y + 32, COL_BLACK);
			if (streq(G_values[idx], "true")) {
				line(x + 2, y + 27, x + 4, y + 30, COL_GREEN);
				line(x + 4, y + 30, x + 9, y + 23, COL_GREEN);
			}
			text(x + 18, y + 20, COL_BLACK, streq(G_values[idx], "true") ? "Enabled" : "Disabled");
		} else if (g_settings[idx].type == SET_TYPE_INT) {
			draw_button(x, y + 20, 22, 20, "-");
			itoa_dec(value_int(idx), num);
			text(x + 34, y + 23, COL_BLACK, num);
			draw_button(x + 80, y + 20, 22, 20, "+");
		} else if (g_settings[idx].type == SET_TYPE_ACTION) {
			draw_button(x, y + 20, 92, 22, "Open");
		}
		row++;
	}

	box(0, WIN_H - 43, WIN_W - 7, WIN_H - 25, COL_LGRAY);
	text(8, WIN_H - 39, COL_NAVY, G_status);
	api_refreshwin(G_win, 3, 21, WIN_W - 3, WIN_H - 3);
}

static void choose_category(int cat)
{
	if (cat < 0 || cat >= N_CATEGORIES) return;
	G_cat = cat;
	G_sel = 0;
}

static void on_key(int key)
{
	int n = setting_count_in_cat();
	int idx;
	if (key == 0x1b || key == 'q') {
		api_end();
	}
	if (key == 0x80) {
		if (G_sel > 0) G_sel--;
	} else if (key == 0x81) {
		if (G_sel < n - 1) G_sel++;
	} else if (key == 0x82) {
		if (G_cat > 0) choose_category(G_cat - 1);
	} else if (key == 0x83) {
		if (G_cat < N_CATEGORIES - 1) choose_category(G_cat + 1);
	} else if (key == 0x0a || key == ' ') {
		idx = setting_index_at(G_sel);
		change_setting(idx, 1);
	} else if (key == '-' || key == '<') {
		idx = setting_index_at(G_sel);
		change_setting(idx, -1);
	} else if (key == '+' || key == '=') {
		idx = setting_index_at(G_sel);
		change_setting(idx, 1);
	}
}

static void on_mouse_down(int x, int y)
{
	int cat, row, idx, relx;
	if (x < SIDE_W) {
		cat = (y - 31) / ROW_H;
		if (0 <= cat && cat < N_CATEGORIES) choose_category(cat);
		return;
	}
	row = (y - 42) / 48;
	idx = setting_index_at(row);
	if (idx < 0) return;
	G_sel = row;
	relx = x - (SIDE_W + 18);
	if (g_settings[idx].type == SET_TYPE_ENUM) {
		int c = relx / 58;
		if (0 <= c && c < g_settings[idx].n_choices) {
			scpy(G_values[idx], g_settings[idx].choices[c].value, sizeof G_values[idx]);
			settings_save();
		}
	} else if (g_settings[idx].type == SET_TYPE_INT) {
		if (20 <= (y - (46 + row * 48)) && (y - (46 + row * 48)) < 40) {
			if (0 <= relx && relx < 22) change_setting(idx, -1);
			if (80 <= relx && relx < 102) change_setting(idx, 1);
		}
	} else {
		change_setting(idx, 1);
	}
}

static void select_cmdline_category(void)
{
	char cmd[64];
	char *p;
	int i;
	api_cmdline(cmd, sizeof cmd);
	for (p = cmd; *p > ' '; p++) { }
	while (*p == ' ') p++;
	for (i = 0; i < N_CATEGORIES; i++) {
		if (streq(p, g_categories[i].id)) {
			G_cat = i;
			return;
		}
	}
}

void HariMain(void)
{
	struct BX_EVENT ev;
	api_initmalloc();
	G_buf = api_malloc(WIN_W * WIN_H);
	G_win = api_openwin(G_buf, WIN_W, WIN_H, -1, "Settings");
	api_set_winevent(G_win, BX_WIN_EV_MOUSE);
	G_cat = 0;
	G_sel = 0;
	settings_load();
	select_cmdline_category();
	draw_settings();
	for (;;) {
		if (api_getevent(&ev, 1) != 1) continue;
		if (ev.type == BX_EVENT_KEY) {
			on_key(ev.key);
		} else if (ev.type == BX_EVENT_MOUSE_DOWN) {
			if (ev.button & 1) on_mouse_down(ev.x, ev.y);
		}
		draw_settings();
	}
}
