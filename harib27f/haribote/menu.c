/* work5 Phase 2: Start Menu primitive */

#include "bootpack.h"
#include <string.h>

static struct SHTCTL *g_menu_ctl;
static struct MEMMAN *g_menu_memman;
static int g_menu_scrnx, g_menu_scrny;
static struct KERNEL_MENU g_menu_root;
static struct KERNEL_MENU g_menu_programs;

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
	g_menu_ctl = shtctl;
	g_menu_memman = memman;
	g_menu_scrnx = scrnx;
	g_menu_scrny = scrny;

	g_menu_root.sht = sheet_alloc(shtctl);
	g_menu_root.parent = 0;
	menu_set_item(&g_menu_root.items[0], "Programs", KMENU_HANDLER_SUBMENU, 1, KMENU_FLAG_SUBMENU, "start/Programs");
	menu_set_item(&g_menu_root.items[1], "Settings", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "settings");
	menu_set_item(&g_menu_root.items[2], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
	menu_set_item(&g_menu_root.items[3], "Run...", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "run");
	menu_set_item(&g_menu_root.items[4], "About BxOS", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "about");
	menu_set_item(&g_menu_root.items[5], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
	menu_set_item(&g_menu_root.items[6], "Restart", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "restart");
	menu_set_item(&g_menu_root.items[7], "Shutdown", KMENU_HANDLER_BUILTIN, 0, KMENU_FLAG_DISABLED, "shutdown");
	menu_init_sheet(&g_menu_root, 0, 8);

	g_menu_programs.sht = sheet_alloc(shtctl);
	g_menu_programs.parent = &g_menu_root;
	menu_set_item(&g_menu_programs.items[0], "Explorer", KMENU_HANDLER_EXEC, 0, 0, "/EXPLORER.HE2");
	menu_set_item(&g_menu_programs.items[1], "Console", KMENU_HANDLER_BUILTIN, 0, 0, "console");
	menu_set_item(&g_menu_programs.items[2], "Tetris", KMENU_HANDLER_EXEC, 0, 0, "/TETRIS.HE2");
	menu_set_item(&g_menu_programs.items[3], "", KMENU_HANDLER_NONE, 0, KMENU_FLAG_SEPARATOR, 0);
	menu_set_item(&g_menu_programs.items[4], "Task Manager", KMENU_HANDLER_BUILTIN, 0, 0, "taskmgr");
	menu_init_sheet(&g_menu_programs, 1, 5);
	g_menu_root.child = 0;
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
	menu_close_child(&g_menu_root);
	if (g_menu_root.sht != 0) {
		sheet_updown(g_menu_root.sht, -1);
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
	if (submenu == 1) {
		return &g_menu_programs;
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
	int y = g_menu_scrny - TASKBAR_HEIGHT - g_menu_root.h;
	if (y < 0) {
		y = 0;
	}
	g_menu_root.x = x;
	g_menu_root.y = y;
	g_menu_root.selected = menu_first_selectable(&g_menu_root);
	menu_close_child(&g_menu_root);
	menu_redraw(&g_menu_root);
	sheet_slide(g_menu_root.sht, x, y);
	menu_raise(&g_menu_root);
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
	struct KERNEL_MENU *menu = &g_menu_root;
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
	if (menu->selected < 0) {
		return;
	}
	item = &menu->items[menu->selected];
	if ((item->flags & KMENU_FLAG_SUBMENU) != 0) {
		menu_open_child(menu);
		return;
	}
	if ((item->flags & (KMENU_FLAG_SEPARATOR | KMENU_FLAG_DISABLED)) == 0) {
		start_menu_close_all();
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
	menus[0] = &g_menu_root;
	menus[1] = g_menu_root.child;
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
