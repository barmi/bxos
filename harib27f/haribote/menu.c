/* work5 Phase 2: Start Menu primitive */

#include "bootpack.h"
#include <string.h>

static struct SHTCTL *g_menu_ctl;
static struct MEMMAN *g_menu_memman;
static int g_menu_scrnx, g_menu_scrny;
static struct KERNEL_MENU g_menus[KMENU_MAX_MENUS];
static char g_menu_sections[KMENU_MAX_MENUS][KMENU_SECTION_MAX];
static int g_menu_count;

#define MENU_CFG_PATH	"/SYSTEM/MENU.CFG"
#define MENU_CFG_BUF_MAX	4096
#define MENU_ITEM_DEF_MAX	64

struct MENU_ITEM_DEF {
	char label[KMENU_LABEL_MAX];
	int handler_id;
	int flags;
	char arg[KMENU_ARG_MAX];
};

static struct MENU_ITEM_DEF g_item_defs[MENU_ITEM_DEF_MAX];
static int g_item_def_count;
static char g_menu_cfg_buf[MENU_CFG_BUF_MAX];

static void menu_putascii(char *vram, int xsize, int x, int y,
		unsigned char c, unsigned char *s)
{
	extern char hankaku[4096];
	for (; *s != 0x00; s++) {
		putfont8(vram, xsize, x, y, c, hankaku + *s * 16);
		x += 8;
	}
	return;
}

static void menu_set_item(struct MENU_ITEM *item, char *label,
		int handler_id, int submenu, int flags, char *arg)
{
	strncpy(item->label, label, KMENU_LABEL_MAX - 1);
	item->label[KMENU_LABEL_MAX - 1] = 0;
	item->handler_id = handler_id;
	item->submenu = submenu;
	item->flags = flags;
	if (arg != 0) {
		strncpy(item->arg, arg, KMENU_ARG_MAX - 1);
		item->arg[KMENU_ARG_MAX - 1] = 0;
	} else {
		item->arg[0] = 0;
	}
	return;
}

static int menu_streq(char *a, char *b)
{
	return strcmp(a, b) == 0;
}

static char *menu_trim(char *s)
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

static void menu_copy(char *dst, int dstsz, char *src)
{
	strncpy(dst, src, dstsz - 1);
	dst[dstsz - 1] = 0;
	return;
}

static void menu_clear_loaded(void)
{
	int i, j;
	g_menu_count = 0;
	g_item_def_count = 0;
	for (i = 0; i < KMENU_MAX_MENUS; i++) {
		g_menu_sections[i][0] = 0;
		g_menus[i].n_items = 0;
		g_menus[i].selected = -1;
		g_menus[i].parent = 0;
		g_menus[i].child = 0;
		g_menus[i].sht = 0;
		for (j = 0; j < KMENU_MAX_ITEMS; j++) {
			g_menus[i].items[j].label[0] = 0;
			g_menus[i].items[j].arg[0] = 0;
			g_menus[i].items[j].handler_id = KMENU_HANDLER_NONE;
			g_menus[i].items[j].submenu = 0;
			g_menus[i].items[j].flags = 0;
		}
	}
	return;
}

static int menu_find_section(char *section)
{
	int i;
	for (i = 0; i < g_menu_count; i++) {
		if (menu_streq(g_menu_sections[i], section)) {
			return i;
		}
	}
	return -1;
}

static int menu_get_section(char *section)
{
	int idx = menu_find_section(section);
	if (idx >= 0) {
		return idx;
	}
	if (g_menu_count >= KMENU_MAX_MENUS) {
		return -1;
	}
	idx = g_menu_count++;
	menu_copy(g_menu_sections[idx], KMENU_SECTION_MAX, section);
	g_menus[idx].level = idx;
	g_menus[idx].n_items = 0;
	g_menus[idx].selected = -1;
	g_menus[idx].parent = 0;
	g_menus[idx].child = 0;
	return idx;
}

static struct MENU_ITEM_DEF *menu_find_item_def(char *label)
{
	int i;
	for (i = 0; i < g_item_def_count; i++) {
		if (menu_streq(g_item_defs[i].label, label)) {
			return &g_item_defs[i];
		}
	}
	return 0;
}

static struct MENU_ITEM_DEF *menu_get_item_def(char *label)
{
	struct MENU_ITEM_DEF *def = menu_find_item_def(label);
	if (def != 0) {
		return def;
	}
	if (g_item_def_count >= MENU_ITEM_DEF_MAX) {
		return 0;
	}
	def = &g_item_defs[g_item_def_count++];
	menu_copy(def->label, KMENU_LABEL_MAX, label);
	def->handler_id = KMENU_HANDLER_NONE;
	def->flags = 0;
	def->arg[0] = 0;
	return def;
}

static int menu_parse_handler(struct MENU_ITEM_DEF *def, char *value)
{
	if (strncmp(value, "exec:", 5) == 0) {
		def->handler_id = KMENU_HANDLER_EXEC;
		def->flags = 0;
		menu_copy(def->arg, KMENU_ARG_MAX, value + 5);
		return 1;
	}
	if (strncmp(value, "builtin:", 8) == 0) {
		def->handler_id = KMENU_HANDLER_BUILTIN;
		def->flags = 0;
		menu_copy(def->arg, KMENU_ARG_MAX, value + 8);
		return 1;
	}
	if (strncmp(value, "settings:", 9) == 0) {
		def->handler_id = KMENU_HANDLER_SETTINGS;
		def->flags = 0;
		menu_copy(def->arg, KMENU_ARG_MAX, value + 9);
		return 1;
	}
	if (strncmp(value, "submenu:", 8) == 0) {
		def->handler_id = KMENU_HANDLER_SUBMENU;
		def->flags = KMENU_FLAG_SUBMENU;
		menu_copy(def->arg, KMENU_ARG_MAX, value + 8);
		return 1;
	}
	def->handler_id = KMENU_HANDLER_NONE;
	def->flags = KMENU_FLAG_DISABLED;
	def->arg[0] = 0;
	return 0;
}

static void menu_parse_items_line(struct KERNEL_MENU *menu, char *value)
{
	char *p = value, *comma, *tok;
	int has_comma;
	menu->n_items = 0;
	for (;;) {
		comma = p;
		while (*comma != 0 && *comma != ',') {
			comma++;
		}
		has_comma = (*comma == ',');
		if (*comma == ',') {
			*comma = 0;
		}
		tok = menu_trim(p);
		if (tok[0] != 0 && menu->n_items < KMENU_MAX_ITEMS) {
			if (menu_streq(tok, "---")) {
				menu_set_item(&menu->items[menu->n_items++], "",
						KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
			} else {
				menu_set_item(&menu->items[menu->n_items++], tok,
						KMENU_HANDLER_NONE, 0, 0, 0);
			}
		}
		if (!has_comma) {
			break;
		}
		p = comma + 1;
	}
	return;
}

static int menu_resolve_loaded(void)
{
	int mi, ii, out, subidx, root_idx;
	struct MENU_ITEM *item;
	struct MENU_ITEM_DEF *def;
	struct KERNEL_MENU tmp_menu;
	char tmp_section[KMENU_SECTION_MAX];

	root_idx = menu_find_section("start");
	if (root_idx < 0 || g_menus[root_idx].n_items <= 0) {
		return 0;
	}
	if (root_idx != 0) {
		tmp_menu = g_menus[0];
		g_menus[0] = g_menus[root_idx];
		g_menus[root_idx] = tmp_menu;
		menu_copy(tmp_section, KMENU_SECTION_MAX, g_menu_sections[0]);
		menu_copy(g_menu_sections[0], KMENU_SECTION_MAX, g_menu_sections[root_idx]);
		menu_copy(g_menu_sections[root_idx], KMENU_SECTION_MAX, tmp_section);
	}

	for (mi = 0; mi < g_menu_count; mi++) {
		out = 0;
		for (ii = 0; ii < g_menus[mi].n_items; ii++) {
			item = &g_menus[mi].items[ii];
			if ((item->flags & KMENU_FLAG_SEPARATOR) != 0) {
				g_menus[mi].items[out++] = *item;
				continue;
			}
			def = menu_find_item_def(item->label);
			if (def == 0 || def->handler_id == KMENU_HANDLER_NONE ||
					(def->flags & KMENU_FLAG_DISABLED) != 0) {
				continue;
			}
			item->handler_id = def->handler_id;
			item->flags = def->flags;
			item->submenu = 0;
			menu_copy(item->arg, KMENU_ARG_MAX, def->arg);
			if (def->handler_id == KMENU_HANDLER_SUBMENU) {
				subidx = menu_find_section(def->arg);
				if (subidx < 0 || g_menus[subidx].n_items <= 0) {
					continue;
				}
				item->submenu = subidx;
				item->flags |= KMENU_FLAG_SUBMENU;
			}
			g_menus[mi].items[out++] = *item;
		}
		g_menus[mi].n_items = out;
	}
	if (g_menus[0].n_items <= 0) {
		return 0;
	}
	return 1;
}

static int menu_load_config(void)
{
	struct FS_FILE file;
	int size, n, line_no = 0;
	char *p, *line, *eq, *section, *key, *value, *end;
	int current_menu = -1;
	struct MENU_ITEM_DEF *current_item = 0;

	menu_clear_loaded();
	if (fs_data_open_path(0, MENU_CFG_PATH, &file) != 0) {
		return 0;
	}
	size = file.finfo.size;
	if (size <= 0 || size >= MENU_CFG_BUF_MAX) {
		return 0;
	}
	n = fs_file_read(&file, 0, g_menu_cfg_buf, size);
	if (n != size) {
		return 0;
	}
	g_menu_cfg_buf[size] = 0;
	p = g_menu_cfg_buf;
	while (*p != 0) {
		line = p;
		while (*p != 0 && *p != '\n') {
			p++;
		}
		if (*p == '\n') {
			*p++ = 0;
		}
		line_no++;
		(void) line_no;
		for (end = line; *end != 0; end++) {
			if (*end == '#' || *end == ';') {
				*end = 0;
				break;
			}
		}
		line = menu_trim(line);
		if (line[0] == 0) {
			continue;
		}
		if (line[0] == '[') {
			end = line + strlen(line) - 1;
			if (end <= line || *end != ']') {
				current_menu = -1;
				current_item = 0;
				continue;
			}
			*end = 0;
			section = menu_trim(line + 1);
			if (strncmp(section, "item:", 5) == 0) {
				current_item = menu_get_item_def(menu_trim(section + 5));
				current_menu = -1;
			} else {
				current_menu = menu_get_section(section);
				current_item = 0;
			}
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
		key = menu_trim(line);
		value = menu_trim(eq + 1);
		if (current_menu >= 0 && menu_streq(key, "items")) {
			menu_parse_items_line(&g_menus[current_menu], value);
		} else if (current_item != 0 && menu_streq(key, "handler")) {
			menu_parse_handler(current_item, value);
		}
	}
	return menu_resolve_loaded();
}

static int menu_first_selectable(struct KERNEL_MENU *menu)
{
	int i;
	for (i = 0; i < menu->n_items; i++) {
		if ((menu->items[i].flags & (KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0) {
			return i;
		}
	}
	return -1;
}

static void menu_draw_item(struct KERNEL_MENU *menu, int idx)
{
	struct MENU_ITEM *item = &menu->items[idx];
	int y = idx * KMENU_ITEM_H;
	int selected = (idx == menu->selected) &&
		((item->flags & (KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0);
	int bg = selected ? COL8_000084 : COL8_C6C6C6;
	int fg = selected ? COL8_FFFFFF : COL8_000000;

	if ((item->flags & KMENU_FLAG_DISABLED) != 0) {
		fg = COL8_848484;
	}
	boxfill8(menu->sht->buf, menu->sht->bxsize, bg, 2, y + 1, menu->w - 3, y + KMENU_ITEM_H - 2);
	if ((item->flags & KMENU_FLAG_SEPARATOR) != 0) {
		boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_848484, 8, y + 10, menu->w - 9, y + 10);
		boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_FFFFFF, 8, y + 11, menu->w - 9, y + 11);
		return;
	}
	menu_putascii(menu->sht->buf, menu->sht->bxsize, 8, y + 3, fg, (unsigned char *) item->label);
	if ((item->flags & KMENU_FLAG_SUBMENU) != 0) {
		menu_putascii(menu->sht->buf, menu->sht->bxsize, menu->w - 16, y + 3, fg, (unsigned char *) ">");
	}
	return;
}

static void menu_redraw(struct KERNEL_MENU *menu)
{
	int i;
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_FFFFFF, 0, 0, menu->w - 2, 0);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_FFFFFF, 0, 0, 0, menu->h - 2);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_848484, 1, menu->h - 2, menu->w - 2, menu->h - 2);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_848484, menu->w - 2, 1, menu->w - 2, menu->h - 2);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_000000, 0, menu->h - 1, menu->w - 1, menu->h - 1);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_000000, menu->w - 1, 0, menu->w - 1, menu->h - 1);
	boxfill8(menu->sht->buf, menu->sht->bxsize, COL8_C6C6C6, 1, 1, menu->w - 3, menu->h - 3);
	for (i = 0; i < menu->n_items; i++) {
		menu_draw_item(menu, i);
	}
	sheet_refresh(menu->sht, 0, 0, menu->w, menu->h);
	return;
}

static void menu_cursor_ensure_visible(void)
{
	if (g_sht_mouse == 0 || g_sht_mouse->height < 0) {
		return;
	}
	if (g_sht_mouse->height != g_menu_ctl->top) {
		sheet_updown(g_sht_mouse, g_menu_ctl->top);
	}
	sheet_slide(g_sht_mouse, g_sht_mouse->vx0, g_sht_mouse->vy0);
	return;
}

static void menu_raise(struct KERNEL_MENU *menu)
{
	int h = g_menu_ctl->top + 1;
	if (g_sht_mouse != 0 && g_sht_mouse->height >= 0) {
		h = g_sht_mouse->height;
	}
	sheet_updown(menu->sht, h);
	menu_cursor_ensure_visible();
	return;
}

static void menu_init_sheet(struct KERNEL_MENU *menu, int level, int n_items)
{
	unsigned char *buf;
	menu->w = KMENU_W;
	menu->h = n_items * KMENU_ITEM_H + 2;
	buf = (unsigned char *) memman_alloc_4k(g_menu_memman, menu->w * menu->h);
	sheet_setbuf(menu->sht, buf, menu->w, menu->h, -1);
	menu->sht->flags &= ~SHEET_FLAG_RESIZABLE;
	menu->sht->flags |= SHEET_FLAG_SYSTEM_WIDGET;
	menu->level = level;
	menu->n_items = n_items;
	menu->selected = menu_first_selectable(menu);
	menu->child = 0;
	return;
}

void start_menu_init(struct SHTCTL *shtctl, struct MEMMAN *memman, int scrnx, int scrny)
{
	int i;
	g_menu_ctl = shtctl;
	g_menu_memman = memman;
	g_menu_scrnx = scrnx;
	g_menu_scrny = scrny;

	if (!menu_load_config()) {
		menu_clear_loaded();
		menu_get_section("start");
		menu_get_section("start/Programs");
		menu_set_item(&g_menus[0].items[0], "Programs", KMENU_HANDLER_SUBMENU, 1, KMENU_FLAG_SUBMENU, "start/Programs");
		menu_set_item(&g_menus[0].items[1], "Settings", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "settings");
		menu_set_item(&g_menus[0].items[2], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
		menu_set_item(&g_menus[0].items[3], "Run...", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "run");
		menu_set_item(&g_menus[0].items[4], "About BxOS", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "about");
		menu_set_item(&g_menus[0].items[5], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
		menu_set_item(&g_menus[0].items[6], "Restart", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "restart");
		menu_set_item(&g_menus[0].items[7], "Shutdown", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "shutdown");
		g_menus[0].n_items = 8;
		menu_set_item(&g_menus[1].items[0], "Explorer", KMENU_HANDLER_EXEC, 0, 0, "/EXPLORER.HE2");
		menu_set_item(&g_menus[1].items[1], "Console", KMENU_HANDLER_BUILTIN, 0, 0, "console");
		menu_set_item(&g_menus[1].items[2], "Tetris", KMENU_HANDLER_EXEC, 0, 0, "/TETRIS.HE2");
		menu_set_item(&g_menus[1].items[3], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
		menu_set_item(&g_menus[1].items[4], "Task Manager", KMENU_HANDLER_BUILTIN, 0, 0, "taskmgr");
		menu_set_item(&g_menus[1].items[5], "Debug", KMENU_HANDLER_BUILTIN, 0, 0, "debug");
		g_menus[1].n_items = 6;
	}
	for (i = 0; i < g_menu_count; i++) {
		g_menus[i].sht = sheet_alloc(shtctl);
		g_menus[i].parent = 0;
		g_menus[i].child = 0;
		menu_init_sheet(&g_menus[i], i, g_menus[i].n_items);
	}
	return;
}

static void menu_close_child(struct KERNEL_MENU *menu)
{
	if (menu != 0 && menu->child != 0) {
		menu_close_child(menu->child);
		sheet_updown(menu->child->sht, -1);
		menu->child = 0;
		menu_cursor_ensure_visible();
	}
	return;
}

void start_menu_close_all(void)
{
	menu_close_child(&g_menus[0]);
	if (g_menus[0].sht != 0) {
		sheet_updown(g_menus[0].sht, -1);
	}
	g_start_menu_open = 0;
	menu_cursor_ensure_visible();
	return;
}

int start_menu_is_open(void)
{
	return g_start_menu_open;
}

static struct KERNEL_MENU *menu_for_submenu(int submenu)
{
	if (0 <= submenu && submenu < g_menu_count) {
		return &g_menus[submenu];
	}
	return 0;
}

static void menu_open_child(struct KERNEL_MENU *parent)
{
	struct MENU_ITEM *item;
	struct KERNEL_MENU *child;
	int x, y;
	if (parent->selected < 0) {
		return;
	}
	item = &parent->items[parent->selected];
	if ((item->flags & KMENU_FLAG_SUBMENU) == 0) {
		menu_close_child(parent);
		return;
	}
	child = menu_for_submenu(item->submenu);
	if (child == 0) {
		return;
	}
	if (parent->child == child && child->sht->height >= 0) {
		menu_cursor_ensure_visible();
		return;
	}
	if (parent->child != child) {
		menu_close_child(parent);
		parent->child = child;
	}
	child->parent = parent;
	child->selected = menu_first_selectable(child);
	child->child = 0;
	x = parent->x + parent->w - 4;
	y = parent->y + parent->selected * KMENU_ITEM_H;
	if (x + child->w > g_menu_scrnx) {
		x = parent->x - child->w + 4;
	}
	if (y + child->h > g_menu_scrny - TASKBAR_HEIGHT) {
		y = g_menu_scrny - TASKBAR_HEIGHT - child->h;
	}
	if (y < 0) {
		y = 0;
	}
	child->x = x;
	child->y = y;
	menu_redraw(child);
	sheet_slide(child->sht, x, y);
	menu_raise(child);
	return;
}

static void menu_open_root(void)
{
	int x = TASKBAR_START_X0;
	int y = g_menu_scrny - TASKBAR_HEIGHT - g_menus[0].h;
	if (y < 0) {
		y = 0;
	}
	g_menus[0].x = x;
	g_menus[0].y = y;
	g_menus[0].selected = menu_first_selectable(&g_menus[0]);
	menu_close_child(&g_menus[0]);
	menu_redraw(&g_menus[0]);
	sheet_slide(g_menus[0].sht, x, y);
	menu_raise(&g_menus[0]);
	g_start_menu_open = 1;
	return;
}

void start_menu_toggle(void)
{
	if (g_start_menu_open) {
		start_menu_close_all();
	} else {
		menu_open_root();
	}
	return;
}

static struct KERNEL_MENU *menu_deepest_open(void)
{
	struct KERNEL_MENU *menu = &g_menus[0];
	while (menu->child != 0 && menu->child->sht->height >= 0) {
		menu = menu->child;
	}
	return menu;
}

static void menu_select_delta(struct KERNEL_MENU *menu, int delta)
{
	int i, n = menu->n_items;
	if (n <= 0) {
		return;
	}
	for (i = 0; i < n; i++) {
		menu->selected += delta;
		if (menu->selected < 0) {
			menu->selected = n - 1;
		}
		if (menu->selected >= n) {
			menu->selected = 0;
		}
		if ((menu->items[menu->selected].flags &
					(KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0) {
			break;
		}
	}
	menu_close_child(menu);
	menu_redraw(menu);
	return;
}

static void menu_invoke(struct KERNEL_MENU *menu)
{
	struct MENU_ITEM *item;
	struct MENU_ITEM invoke_item;
	if (menu->selected < 0) {
		return;
	}
	item = &menu->items[menu->selected];
	if ((item->flags & KMENU_FLAG_SUBMENU) != 0) {
		menu_open_child(menu);
		return;
	}
	if ((item->flags & (KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0) {
		invoke_item = *item;
		start_menu_close_all();
		start_menu_dispatch(&invoke_item);
	}
	return;
}

int start_menu_handle_key(int key)
{
	struct KERNEL_MENU *menu;
	if (!g_start_menu_open) {
		return 0;
	}
	menu = menu_deepest_open();
	if (key == 0x48) {	/* Up */
		menu_select_delta(menu, -1);
		return 1;
	}
	if (key == 0x50) {	/* Down */
		menu_select_delta(menu, 1);
		return 1;
	}
	if (key == 0x4d) {	/* Right */
		menu_open_child(menu);
		return 1;
	}
	if (key == 0x4b) {	/* Left */
		if (menu->parent != 0) {
			sheet_updown(menu->sht, -1);
			menu->parent->child = 0;
			menu_cursor_ensure_visible();
		}
		return 1;
	}
	if (key == 0x1c) {	/* Enter */
		menu_invoke(menu);
		return 1;
	}
	if (key == 0x01) {	/* Esc */
		if (menu->parent != 0) {
			sheet_updown(menu->sht, -1);
			menu->parent->child = 0;
			menu_cursor_ensure_visible();
		} else {
			start_menu_close_all();
		}
		return 1;
	}
	return 0;
}

static struct KERNEL_MENU *menu_hit(int mx, int my, int *item_idx)
{
	struct KERNEL_MENU *menus[2];
	int i, x, y;
	menus[0] = &g_menus[0];
	menus[1] = g_menus[0].child;
	for (i = 1; i >= 0; i--) {
		struct KERNEL_MENU *menu = menus[i];
		if (menu == 0 || menu->sht->height < 0) {
			continue;
		}
		x = mx - menu->x;
		y = my - menu->y;
		if (0 <= x && x < menu->w && 0 <= y && y < menu->h) {
			*item_idx = y / KMENU_ITEM_H;
			if (*item_idx >= menu->n_items) {
				*item_idx = menu->n_items - 1;
			}
			return menu;
		}
	}
	return 0;
}

int start_menu_handle_mouse(int mx, int my, int btn, int old_btn)
{
	struct KERNEL_MENU *menu;
	int idx = -1, btn_dn, btn_up;
	if (!g_start_menu_open) {
		return 0;
	}
	menu = menu_hit(mx, my, &idx);
	btn_dn = (~old_btn & btn) & 0x01;
	btn_up = (old_btn & ~btn) & 0x01;
	if (menu == 0) {
		if (btn_dn != 0) {
			start_menu_close_all();
		}
		return 0;
	}
	if (0 <= idx && idx < menu->n_items &&
			(menu->items[idx].flags & (KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0) {
		if (idx != menu->selected) {
			menu->selected = idx;
			menu_close_child(menu);
			menu_redraw(menu);
		}
		if ((menu->items[idx].flags & KMENU_FLAG_SUBMENU) != 0) {
			menu_open_child(menu);
		}
		if (btn_up != 0) {
			menu_invoke(menu);
		}
	}
	return 1;
}
