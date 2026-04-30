/* explorer.c — BxOS work4 Phase 3: 2-pane 파일 탐색기 (읽기 전용 MVP).
 *
 *   상단    : toolbar placeholder + 현재 path 표시
 *   왼쪽    : directory tree (lazy expand)
 *   가운데  : splitter (드래그는 Phase 4)
 *   오른쪽  : file list (디렉터리 우선 + 이름순)
 *   하단    : status bar
 *
 * 키보드 (Phase 3 MVP):
 *   ↑/↓    selection 이동
 *   ← / Backspace   부모 디렉터리(트리) / collapse
 *   →      tree 노드 expand (또는 file list 진입)
 *   Enter  디렉터리 진입 / 그 외 항목은 status 표시 (Phase 5 에서 preview/exec)
 *   Tab    tree ↔ list focus 전환
 *   r      현재 디렉터리 reload
 *   q      종료
 *
 * 마우스 (Phase 3 MVP):
 *   tree/list 의 row 를 single-click → 선택 (포커스도 그 pane 으로 이동).
 *   같은 row 를 한번 더 click → "open" (트리 = expand toggle, 리스트 = 디렉터리 진입).
 *   resize edge drag 종료 시 RESIZE 이벤트로 layout 재계산 + redraw.            */
#include "apilib.h"

/* ---- 상수 ----------------------------------------------------------------- */

#define MAX_TREE_NODES   256
#define MAX_FILES        256
#define MAX_PATH         128

#define WIN_W_DEF        420
#define WIN_H_DEF        280
#define WIN_W_MIN        320
#define WIN_H_MIN        200

#define FRAME_L          3
#define FRAME_R          3
#define FRAME_T          21
#define FRAME_B          3

#define TOOLBAR_H        22
#define STATUS_H         18
#define ROW_H            16
#define SPLIT_W          4
#define TREE_INDENT      8
#define TREE_RATIO_DEF   32   /* % */
#define TREE_W_MIN       96

/* color (graphic.c table_rgb) */
#define COL_BG           7    /* white   */
#define COL_TEXT         0    /* black   */
#define COL_GRAY         8    /* lt gray */
#define COL_TBAR_BG      8
#define COL_STAT_BG      8
#define COL_BORDER       0
#define COL_SEL_FOC      4    /* blue    */
#define COL_SEL_NOF      8    /* gray    */
#define COL_SEL_FG       7    /* white   */
#define COL_DIR          12   /* dark blue */
#define COL_ERR          1    /* red     */

#define KEY_UP           0x80
#define KEY_DOWN         0x81
#define KEY_LEFT         0x82
#define KEY_RIGHT        0x83
#define KEY_ENTER        0x0a
#define KEY_BS           0x08
#define KEY_TAB          0x09
#define KEY_ESC          0x1b

#define FOCUS_TREE       0
#define FOCUS_LIST       1

/* ---- 상태 ----------------------------------------------------------------- */

struct ExpNode {
    char path[MAX_PATH];
    int  depth;
    int  expanded;
};

struct AppState {
    int  win;
    int  w, h;
    int  cw, ch;        /* client area size */
    char *buf;
    int  buf_size;
    int  tree_w;        /* tree pane width in client coords */

    struct ExpNode tree[MAX_TREE_NODES];
    int  tree_count;
    int  tree_sel;
    int  tree_top;

    struct BX_DIRINFO files[MAX_FILES];
    int  files_count;
    int  files_sel;
    int  files_top;

    int  focus;
    char status[80];
    int  status_err;

    /* 같은-row 두 번 click → open. Mouse pane: 0=tree,1=list,-1=none */
    int  last_click_pane;
    int  last_click_idx;
};

static struct AppState G;

/* ---- 일반 유틸 ------------------------------------------------------------ */

static int s_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int s_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] != 0 || b[i] != 0; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static void s_cpy(char *dst, const char *src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i] != 0; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void path_join(char *out, const char *base, const char *name)
{
    int i = 0, j;
    if (base[0] == '/' && base[1] == 0) {
        if (i < MAX_PATH - 1) out[i++] = '/';
    } else {
        for (j = 0; base[j] != 0 && i < MAX_PATH - 1; j++) out[i++] = base[j];
        if (i < MAX_PATH - 1) out[i++] = '/';
    }
    for (j = 0; name[j] != 0 && i < MAX_PATH - 1; j++) out[i++] = name[j];
    out[i] = 0;
}

static void path_parent(const char *p, char *out)
{
    int n = s_len(p);
    int i, j;
    if (n <= 1) { out[0] = '/'; out[1] = 0; return; }
    for (i = n - 1; i > 0; i--) {
        if (p[i] == '/') {
            for (j = 0; j < i; j++) out[j] = p[j];
            out[i] = 0;
            if (out[0] == 0) { out[0] = '/'; out[1] = 0; }
            return;
        }
    }
    out[0] = '/'; out[1] = 0;
}

static const char *path_basename(const char *p)
{
    int n = s_len(p);
    int i;
    for (i = n - 1; i >= 0; i--)
        if (p[i] == '/') return p + i + 1;
    return p;
}

static void int_to_str(unsigned int v, char *out)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && i < 11) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (--i >= 0) out[j++] = tmp[i];
    out[j] = 0;
}

static int dirinfo_cmp(const struct BX_DIRINFO *a, const struct BX_DIRINFO *b)
{
    int da = (a->attr & 0x10) ? 1 : 0;
    int db = (b->attr & 0x10) ? 1 : 0;
    int i;
    if (da != db) return db - da;       /* directory first */
    for (i = 0; i < 13; i++) {
        unsigned char ca = (unsigned char) a->name[i];
        unsigned char cb = (unsigned char) b->name[i];
        if (ca != cb) return (int) ca - (int) cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static void dirinfo_sort(struct BX_DIRINFO *arr, int n)
{
    int i, j;
    struct BX_DIRINFO tmp;
    for (i = 1; i < n; i++) {
        tmp = arr[i];
        j = i - 1;
        while (j >= 0 && dirinfo_cmp(&arr[j], &tmp) > 0) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = tmp;
    }
}

static int is_dot_or_dotdot(const char *name)
{
    return name[0] == '.' &&
            (name[1] == 0 || (name[1] == '.' && name[2] == 0));
}

/* ---- 파일 목록 --------------------------------------------------------- */

static void files_load(const char *path)
{
    int h, r, n = 0;
    struct BX_DIRINFO ent;
    G.files_count = 0;
    G.files_sel = 0;
    G.files_top = 0;
    h = api_opendir((char *) path);
    if (h == 0) {
        G.status_err = 1;
        s_cpy(G.status, "opendir failed", sizeof G.status);
        return;
    }
    for (;;) {
        r = api_readdir(h, &ent);
        if (r <= 0) break;
        if (is_dot_or_dotdot(ent.name)) continue;
        if (n < MAX_FILES) {
            G.files[n++] = ent;
        }
    }
    api_closedir(h);
    dirinfo_sort(G.files, n);
    G.files_count = n;
}

/* ---- 트리 -------------------------------------------------------------- */

static int tree_find(const char *path)
{
    int i;
    for (i = 0; i < G.tree_count; i++)
        if (s_eq(G.tree[i].path, path)) return i;
    return -1;
}

static void tree_collapse_at(int idx)
{
    int rd = G.tree[idx].depth;
    int j = idx + 1;
    int rm, k;
    while (j < G.tree_count && G.tree[j].depth > rd) j++;
    rm = j - (idx + 1);
    for (k = j; k < G.tree_count; k++) G.tree[k - rm] = G.tree[k];
    G.tree_count -= rm;
    G.tree[idx].expanded = 0;
}

static int tree_expand_at(int idx)
{
    struct BX_DIRINFO sub[MAX_FILES];
    struct BX_DIRINFO ent;
    int h, r, i, j, n = 0;

    if (G.tree[idx].expanded) return 0;
    h = api_opendir(G.tree[idx].path);
    if (h == 0) return -1;
    for (;;) {
        r = api_readdir(h, &ent);
        if (r <= 0) break;
        if ((ent.attr & 0x10) == 0) continue;
        if (is_dot_or_dotdot(ent.name)) continue;
        if (n >= MAX_FILES) break;
        sub[n++] = ent;
    }
    api_closedir(h);
    dirinfo_sort(sub, n);
    if (G.tree_count + n > MAX_TREE_NODES) {
        n = MAX_TREE_NODES - G.tree_count;
        if (n < 0) n = 0;
    }
    for (j = G.tree_count - 1; j > idx; j--)
        G.tree[j + n] = G.tree[j];
    for (i = 0; i < n; i++) {
        path_join(G.tree[idx + 1 + i].path, G.tree[idx].path, sub[i].name);
        G.tree[idx + 1 + i].depth = G.tree[idx].depth + 1;
        G.tree[idx + 1 + i].expanded = 0;
    }
    G.tree_count += n;
    G.tree[idx].expanded = 1;
    return n;
}

static void tree_init(const char *target)
{
    char comp[MAX_PATH];
    char cur[MAX_PATH];
    char nxt[MAX_PATH];
    int i, k, found;

    s_cpy(G.tree[0].path, "/", MAX_PATH);
    G.tree[0].depth = 0;
    G.tree[0].expanded = 0;
    G.tree_count = 1;
    G.tree_sel = 0;
    G.tree_top = 0;
    tree_expand_at(0);
    if (target == 0 || target[0] == 0) return;
    if (target[0] == '/' && target[1] == 0) return;
    s_cpy(cur, "/", MAX_PATH);
    i = 0;
    if (target[0] == '/') i = 1;
    while (target[i] != 0) {
        while (target[i] == '/') i++;
        if (target[i] == 0) break;
        k = 0;
        while (target[i] != '/' && target[i] != 0 && k < MAX_PATH - 1)
            comp[k++] = target[i++];
        comp[k] = 0;
        path_join(nxt, cur, comp);
        found = tree_find(nxt);
        if (found < 0) break;
        G.tree_sel = found;
        tree_expand_at(found);
        s_cpy(cur, nxt, MAX_PATH);
    }
}

/* ---- 레이아웃 ---------------------------------------------------------- */

static void compute_layout(void)
{
    int avail;
    G.cw = G.w - FRAME_L - FRAME_R;
    G.ch = G.h - FRAME_T - FRAME_B;
    avail = G.cw - SPLIT_W;
    if (G.tree_w == 0) G.tree_w = avail * TREE_RATIO_DEF / 100;
    if (G.tree_w < TREE_W_MIN) G.tree_w = TREE_W_MIN;
    if (G.tree_w > avail - 80) G.tree_w = avail - 80;
    if (G.tree_w < TREE_W_MIN) G.tree_w = TREE_W_MIN;
}

static int mid_top(void)    { return TOOLBAR_H; }
static int mid_bot(void)    { return G.ch - STATUS_H; }
static int mid_h(void)      { return mid_bot() - mid_top(); }
static int rows_visible(void) { return mid_h() / ROW_H; }

/* ---- 그리기 헬퍼 (client → sheet 좌표) -------------------------------- */

static int s_x(int x) { return x + FRAME_L; }
static int s_y(int y) { return y + FRAME_T; }

static void cli_box(int x0, int y0, int x1, int y1, int col)
{
    api_boxfilwin(G.win | 1, s_x(x0), s_y(y0), s_x(x1), s_y(y1), col);
}

static void cli_str(int x, int y, int col, int len, const char *s)
{
    api_putstrwin(G.win | 1, s_x(x), s_y(y), col, len, (char *) s);
}

static void cli_refresh(int x0, int y0, int x1, int y1)
{
    api_refreshwin(G.win, s_x(x0), s_y(y0), s_x(x1), s_y(y1));
}

/* 화면 폭에 맞춰 문자열을 잘라 표시. col 갯수만큼 글자만 표시.
 * 실제 폭이 부족하면 마지막 1~2 글자를 ".." 으로 대체.                       */
static int trunc_to_cols(char *dst, const char *src, int cols)
{
    int i, sl = s_len(src);
    if (cols <= 0) { dst[0] = 0; return 0; }
    if (sl <= cols) {
        for (i = 0; i < sl; i++) dst[i] = src[i];
        dst[i] = 0;
        return sl;
    }
    if (cols <= 2) {
        for (i = 0; i < cols; i++) dst[i] = '.';
        dst[i] = 0;
        return cols;
    }
    for (i = 0; i < cols - 2; i++) dst[i] = src[i];
    dst[cols - 2] = '.';
    dst[cols - 1] = '.';
    dst[cols] = 0;
    return cols;
}

/* ---- pane 그리기 ------------------------------------------------------ */

static void draw_toolbar(void)
{
    char tmp[64];
    char path[MAX_PATH];
    int n;
    /* background */
    cli_box(0, 0, G.cw - 1, TOOLBAR_H - 1, COL_TBAR_BG);
    /* placeholder "buttons": Back / Up / Refresh / New / Del / Ren */
    {
        int x = 4, w = 26, y0 = 3, y1 = TOOLBAR_H - 4;
        const char *labels[6] = { "<-", "..", "R", "N", "D", "M" };
        int j;
        for (j = 0; j < 6; j++) {
            cli_box(x, y0, x + w - 1, y1, COL_BG);
            cli_box(x, y0, x + w - 1, y0, COL_BORDER);
            cli_box(x, y1, x + w - 1, y1, COL_BORDER);
            cli_box(x, y0, x, y1, COL_BORDER);
            cli_box(x + w - 1, y0, x + w - 1, y1, COL_BORDER);
            cli_str(x + 4, y0 + 2, COL_TEXT, 2, labels[j]);
            x += w + 2;
        }
        /* 현재 path 라벨 */
        s_cpy(path,
                G.tree_count > 0 ? G.tree[G.tree_sel].path : "/",
                MAX_PATH);
        n = trunc_to_cols(tmp, path, (G.cw - x - 8) / 8);
        cli_str(x + 6, y0 + 2, COL_TEXT, n, tmp);
    }
}

static void draw_status(void)
{
    char tmp[64];
    int y0 = G.ch - STATUS_H, y1 = G.ch - 1;
    int n;
    int col = G.status_err ? COL_ERR : COL_TEXT;
    cli_box(0, y0, G.cw - 1, y1, COL_STAT_BG);
    n = trunc_to_cols(tmp, G.status, (G.cw - 8) / 8);
    cli_str(4, y0 + 2, col, n, tmp);
}

static void draw_tree(void)
{
    int top = mid_top(), bot = mid_bot();
    int x0 = 0, x1 = G.tree_w - 1;
    int rows = rows_visible();
    int i, r;
    int max_cols;

    /* clamp top */
    if (G.tree_sel < G.tree_top) G.tree_top = G.tree_sel;
    if (G.tree_sel >= G.tree_top + rows) G.tree_top = G.tree_sel - rows + 1;
    if (G.tree_top < 0) G.tree_top = 0;

    cli_box(x0, top, x1, bot - 1, COL_BG);
    max_cols = (G.tree_w - 4) / 8;
    for (r = 0; r < rows; r++) {
        i = G.tree_top + r;
        if (i >= G.tree_count) break;
        {
            struct ExpNode *nd = &G.tree[i];
            int row_y = top + r * ROW_H;
            int indent = nd->depth * TREE_INDENT;
            const char *bn = (nd->depth == 0) ? "/" : path_basename(nd->path);
            char tmp[40];
            int n;
            int avail = max_cols - (indent / 8) - 2;
            int sel = (i == G.tree_sel);
            int sel_col = (G.focus == FOCUS_TREE) ? COL_SEL_FOC : COL_SEL_NOF;
            int fg = sel ? COL_SEL_FG : COL_TEXT;
            if (sel) {
                cli_box(x0, row_y, x1, row_y + ROW_H - 1, sel_col);
            }
            /* expand marker */
            {
                char m[2]; m[1] = 0;
                m[0] = nd->expanded ? '-' : '+';
                cli_str(2 + indent, row_y + 1, fg, 1, m);
            }
            if (avail < 1) avail = 1;
            n = trunc_to_cols(tmp, bn, avail);
            cli_str(2 + indent + 16, row_y + 1, fg, n, tmp);
        }
    }
    /* splitter */
    cli_box(G.tree_w, top, G.tree_w + SPLIT_W - 1, bot - 1, COL_GRAY);
}

static void draw_list(void)
{
    int top = mid_top(), bot = mid_bot();
    int x0 = G.tree_w + SPLIT_W, x1 = G.cw - 1;
    int rows = rows_visible();
    int max_cols;
    int r, i;

    if (G.files_sel < G.files_top) G.files_top = G.files_sel;
    if (G.files_sel >= G.files_top + rows) G.files_top = G.files_sel - rows + 1;
    if (G.files_top < 0) G.files_top = 0;

    cli_box(x0, top, x1, bot - 1, COL_BG);
    max_cols = (x1 - x0 - 4) / 8;
    /* 헤더 (간단히) */
    if (rows > 0) {
        cli_box(x0, top, x1, top + ROW_H - 1, COL_GRAY);
        cli_str(x0 + 4, top + 1, COL_TEXT, 4, "Name");
        if (max_cols > 18) {
            cli_str(x0 + 4 + (max_cols - 8) * 8, top + 1, COL_TEXT,
                    4, "Size");
        }
    }
    for (r = 1; r < rows; r++) {
        i = G.files_top + (r - 1);
        if (i >= G.files_count) break;
        {
            struct BX_DIRINFO *ent = &G.files[i];
            int row_y = top + r * ROW_H;
            int sel = (i == G.files_sel);
            int sel_col = (G.focus == FOCUS_LIST) ? COL_SEL_FOC : COL_SEL_NOF;
            int isdir = (ent->attr & 0x10) != 0;
            int fg = sel ? COL_SEL_FG : (isdir ? COL_DIR : COL_TEXT);
            char nm[16];
            int n;
            char sz[16];
            if (sel) {
                cli_box(x0, row_y, x1, row_y + ROW_H - 1, sel_col);
            }
            n = trunc_to_cols(nm, ent->name, 12);
            cli_str(x0 + 4, row_y + 1, fg, n, nm);
            if (isdir) {
                cli_str(x0 + 4 + 13 * 8, row_y + 1, fg, 5, "<DIR>");
            } else if (max_cols > 18) {
                int_to_str(ent->size, sz);
                cli_str(x0 + 4 + (max_cols - 8) * 8, row_y + 1,
                        fg, s_len(sz), sz);
            }
        }
    }
}

static void redraw_all(void)
{
    /* 외곽 frame 은 make_window8 가 이미 그림. client 영역 재칠.            */
    draw_toolbar();
    draw_tree();
    draw_list();
    draw_status();
    cli_refresh(0, 0, G.cw, G.ch);
}

/* ---- 디렉터리 진입/이동 ------------------------------------------------ */

static void status_set(const char *s, int err)
{
    s_cpy(G.status, s, sizeof G.status);
    G.status_err = err ? 1 : 0;
}

static void update_files_for_selection(void)
{
    if (G.tree_count == 0) return;
    files_load(G.tree[G.tree_sel].path);
    if (!G.status_err) {
        char tmp[80], num[12];
        int_to_str(G.files_count, num);
        s_cpy(tmp, "entries: ", sizeof tmp);
        {
            int i = s_len(tmp), j;
            for (j = 0; num[j] && i < (int) sizeof tmp - 1; j++) tmp[i++] = num[j];
            tmp[i] = 0;
        }
        status_set(tmp, 0);
    }
}

static void enter_dir_path(const char *path)
{
    int idx = tree_find(path);
    if (idx < 0) {
        /* try to chain expand from root */
        tree_init(path);
        idx = tree_find(path);
        if (idx < 0) idx = 0;
    } else {
        if (!G.tree[idx].expanded) tree_expand_at(idx);
    }
    G.tree_sel = idx;
    update_files_for_selection();
}

/* ---- 입력 처리 ---------------------------------------------------------- */

static void on_key(int key)
{
    char parent[MAX_PATH];
    if (key == 'q' || key == KEY_ESC) {
        api_closewin(G.win);
        api_end();
    }
    if (key == KEY_TAB) {
        G.focus = (G.focus == FOCUS_TREE) ? FOCUS_LIST : FOCUS_TREE;
        return;
    }
    if (key == 'r') {
        update_files_for_selection();
        return;
    }
    if (G.focus == FOCUS_TREE) {
        if (key == KEY_UP) {
            if (G.tree_sel > 0) G.tree_sel--;
            update_files_for_selection();
        } else if (key == KEY_DOWN) {
            if (G.tree_sel + 1 < G.tree_count) G.tree_sel++;
            update_files_for_selection();
        } else if (key == KEY_LEFT || key == KEY_BS) {
            struct ExpNode *nd = &G.tree[G.tree_sel];
            if (nd->expanded) {
                tree_collapse_at(G.tree_sel);
            } else if (nd->depth > 0) {
                /* parent 로 이동 */
                path_parent(nd->path, parent);
                {
                    int p = tree_find(parent);
                    if (p >= 0) G.tree_sel = p;
                }
                update_files_for_selection();
            }
        } else if (key == KEY_RIGHT) {
            struct ExpNode *nd = &G.tree[G.tree_sel];
            if (!nd->expanded) tree_expand_at(G.tree_sel);
        } else if (key == KEY_ENTER) {
            struct ExpNode *nd = &G.tree[G.tree_sel];
            if (nd->expanded) {
                tree_collapse_at(G.tree_sel);
            } else {
                tree_expand_at(G.tree_sel);
            }
        }
    } else {
        /* FOCUS_LIST */
        if (key == KEY_UP) {
            if (G.files_sel > 0) G.files_sel--;
        } else if (key == KEY_DOWN) {
            if (G.files_sel + 1 < G.files_count) G.files_sel++;
        } else if (key == KEY_LEFT || key == KEY_BS) {
            /* 부모로 */
            path_parent(G.tree[G.tree_sel].path, parent);
            enter_dir_path(parent);
        } else if (key == KEY_ENTER || key == KEY_RIGHT) {
            if (G.files_count > 0) {
                struct BX_DIRINFO *e = &G.files[G.files_sel];
                if ((e->attr & 0x10) != 0) {
                    char child[MAX_PATH];
                    path_join(child, G.tree[G.tree_sel].path, e->name);
                    enter_dir_path(child);
                } else {
                    char tmp[80];
                    int n = s_len(e->name);
                    if (n > 12) n = 12;
                    s_cpy(tmp, "open: ", sizeof tmp);
                    {
                        int i = s_len(tmp), j;
                        for (j = 0; j < n && i < (int) sizeof tmp - 1; j++)
                            tmp[i++] = e->name[j];
                        if (i < (int) sizeof tmp - 1) tmp[i++] = ' ';
                        if (i < (int) sizeof tmp - 1) tmp[i++] = '(';
                        if (i < (int) sizeof tmp - 1) tmp[i++] = 'P';
                        if (i < (int) sizeof tmp - 1) tmp[i++] = '5';
                        if (i < (int) sizeof tmp - 1) tmp[i++] = ')';
                        tmp[i] = 0;
                    }
                    status_set(tmp, 0);
                }
            }
        }
    }
}

static int hit_pane(int cx, int cy)
{
    int top = mid_top(), bot = mid_bot();
    if (cx < 0 || cy < top || cy >= bot) return -1;
    if (cx < G.tree_w) return FOCUS_TREE;
    if (cx >= G.tree_w + SPLIT_W && cx < G.cw) return FOCUS_LIST;
    return -1;
}

static void on_mouse_down(int cx, int cy)
{
    int pane = hit_pane(cx, cy);
    int top = mid_top();
    if (pane == FOCUS_TREE) {
        int row = (cy - top) / ROW_H;
        int idx = G.tree_top + row;
        G.focus = FOCUS_TREE;
        if (idx >= 0 && idx < G.tree_count) {
            int dbl = (G.last_click_pane == FOCUS_TREE && G.last_click_idx == idx);
            G.tree_sel = idx;
            if (dbl) {
                struct ExpNode *nd = &G.tree[G.tree_sel];
                if (nd->expanded) tree_collapse_at(G.tree_sel);
                else              tree_expand_at(G.tree_sel);
                G.last_click_pane = -1;
            } else {
                G.last_click_pane = FOCUS_TREE;
                G.last_click_idx = idx;
            }
            update_files_for_selection();
        }
    } else if (pane == FOCUS_LIST) {
        /* row=0 은 헤더, 1+ 부터 실제 row */
        int row = (cy - top) / ROW_H;
        int idx;
        G.focus = FOCUS_LIST;
        if (row <= 0) return;
        idx = G.files_top + (row - 1);
        if (idx >= 0 && idx < G.files_count) {
            int dbl = (G.last_click_pane == FOCUS_LIST && G.last_click_idx == idx);
            G.files_sel = idx;
            if (dbl) {
                struct BX_DIRINFO *e = &G.files[G.files_sel];
                if ((e->attr & 0x10) != 0) {
                    char child[MAX_PATH];
                    path_join(child, G.tree[G.tree_sel].path, e->name);
                    enter_dir_path(child);
                }
                G.last_click_pane = -1;
            } else {
                G.last_click_pane = FOCUS_LIST;
                G.last_click_idx = idx;
            }
        }
    } else {
        G.last_click_pane = -1;
    }
}

static void on_resize(int new_w, int new_h)
{
    int nw = new_w, nh = new_h;
    char *new_buf;
    int new_size;
    if (nw < WIN_W_MIN) nw = WIN_W_MIN;
    if (nh < WIN_H_MIN) nh = WIN_H_MIN;
    new_size = nw * nh;
    new_buf = api_malloc(new_size);
    if (new_buf == 0) {
        status_set("resize failed (oom)", 1);
        return;
    }
    {
        int old_w = G.w, old_h = G.h;
        char *old_buf = G.buf;
        int old_size = G.buf_size;
        G.w = nw;
        G.h = nh;
        G.buf = new_buf;
        G.buf_size = new_size;
        api_resizewin(G.win, new_buf, nw, nh, -1);
        compute_layout();
        api_free(old_buf, old_size);
        (void) old_w; (void) old_h;
    }
}

/* ---- main --------------------------------------------------------------- */

void HariMain(void)
{
    char cmdline[MAX_PATH];
    char start_path[MAX_PATH];
    char *p;
    int i;
    struct BX_EVENT ev;

    api_initmalloc();

    /* 시작 경로 결정 */
    api_cmdline(cmdline, sizeof cmdline);
    for (p = cmdline; *p > ' '; p++) { }
    while (*p == ' ') p++;
    if (*p == 0) {
        if (api_getcwd(start_path, sizeof start_path) <= 0) {
            s_cpy(start_path, "/", sizeof start_path);
        }
        if (start_path[0] == 0) s_cpy(start_path, "/", sizeof start_path);
    } else {
        for (i = 0; i < (int) sizeof start_path - 1 && p[i] != 0 && p[i] != ' '; i++) {
            start_path[i] = p[i];
        }
        start_path[i] = 0;
    }

    /* 윈도우 */
    G.w = WIN_W_DEF;
    G.h = WIN_H_DEF;
    G.tree_w = 0;
    G.focus = FOCUS_TREE;
    G.last_click_pane = -1;
    G.last_click_idx = -1;
    G.buf_size = G.w * G.h;
    G.buf = api_malloc(G.buf_size);
    G.win = api_openwin(G.buf, G.w, G.h, -1, "Explorer");
    api_set_winevent(G.win, BX_WIN_EV_MOUSE | BX_WIN_EV_RESIZE);

    compute_layout();
    tree_init(start_path);
    update_files_for_selection();
    redraw_all();

    for (;;) {
        if (api_getevent(&ev, 1) != 1) continue;
        if (ev.type == BX_EVENT_KEY) {
            on_key(ev.key);
        } else if (ev.type == BX_EVENT_MOUSE_DOWN) {
            if (ev.button & 1) on_mouse_down(ev.x, ev.y);
        } else if (ev.type == BX_EVENT_RESIZE) {
            on_resize(ev.w, ev.h);
        }
        redraw_all();
    }
}
