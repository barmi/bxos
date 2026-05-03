/* explorer.c — BxOS work4: 2-pane 파일 탐색기.
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
 *   Enter  디렉터리 진입 / .HE2 실행 / 파일 preview
 *   Tab    tree ↔ list focus 전환
 *   r      현재 디렉터리 reload
 *   n/d/m  새 디렉터리 / 삭제 / rename
 *   q      종료
 *
 * 마우스 (Phase 3 MVP):
 *   tree/list 의 row 를 single-click → 선택 (포커스도 그 pane 으로 이동).
 *   같은 row 를 한번 더 click → "open" (트리 = expand toggle, 리스트 = 디렉터리 진입).
 *   splitter drag 로 tree/list 폭 조절.
 *   resize edge drag 종료 시 RESIZE 이벤트로 layout 재계산 + redraw.            */
#include "apilib.h"

/* ---- 상수 ----------------------------------------------------------------- */

#define MAX_TREE_NODES   256
#define MAX_FILES        256
#define MAX_PATH         128
#define PREVIEW_MAX      1024

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
#define SCROLL_W         8
#define SCROLL_MIN_THUMB 10
#define TREE_INDENT      8
#define TREE_RATIO_DEF   320  /* permille */
#define TREE_W_MIN       96
#define LIST_W_MIN       80

/* color (graphic.c table_rgb)
 *   COL8_FFFFFF=7 흰색 (raised TL / sunken BR)
 *   COL8_C6C6C6=8 light gray (chrome 면 색)
 *   COL8_848484=15 dark gray (raised BR / sunken TL)
 *   COL8_000000=0 검정
 */
#define COL_WHITE        7
#define COL_LGRAY        8
#define COL_DGRAY        15
#define COL_BLACK        0
#define COL_BG           7    /* white (list/tree 배경) */
#define COL_TEXT         0
#define COL_CHROME       8    /* toolbar / status / splitter 면 색 */
#define COL_BORDER       0
#define COL_SEL_FOC      4    /* blue    */
#define COL_SEL_NOF      8    /* gray    */
#define COL_SEL_FG       7    /* white   */
#define COL_DIR          12   /* dark blue */
#define COL_EXEC         10   /* dark green */
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
#define HIT_SPLITTER     2
#define SCROLL_NONE      0
#define SCROLL_TREE      1
#define SCROLL_LIST      2
#define SCROLL_PREVIEW   3
#define MODE_LIST        0
#define MODE_PREVIEW     1
#define MODE_INPUT       2
#define MODE_CONFIRM     3
#define ACT_NONE         0
#define ACT_MKDIR        1
#define ACT_DELETE       2
#define ACT_RENAME       3

/* ---- 상태 ----------------------------------------------------------------- */

struct ExpNode {
    char path[MAX_PATH];
    int  depth;
    int  expanded;
};

struct Rect {
    int x, y, w, h;
};

struct Layout {
    struct Rect toolbar;
    struct Rect middle;
    struct Rect tree;
    struct Rect splitter;
    struct Rect list;
    struct Rect status;
};

struct AppState {
    int  win;
    int  w, h;
    int  cw, ch;        /* client area size */
    char *buf;
    int  buf_size;
    int  tree_w;        /* tree pane width in client coords */
    int  tree_ratio;    /* permille, updated by splitter drag */
    struct Layout layout;

    struct ExpNode tree[MAX_TREE_NODES];
    int  tree_count;
    int  tree_sel;
    int  tree_top;

    struct BX_DIRINFO files[MAX_FILES];
    int  files_count;
    int  files_sel;
    int  files_top;

    int  mode;
    char preview_path[MAX_PATH];
    char preview_buf[PREVIEW_MAX + 1];
    int  preview_len;
    int  preview_size;
    int  preview_trunc;
    int  preview_binary;
    int  preview_top;

    int  action;
    char input_prompt[24];
    char input_buf[MAX_PATH];
    int  input_len;
    char op_path[MAX_PATH];
    char op_name[13];
    unsigned char op_attr;
    int  op_sel;

    int  focus;
    char status[80];
    int  status_err;
    int  status_transient;

    /* 같은-row 두 번 click → open. Mouse pane: 0=tree,1=list,-1=none */
    int  last_click_pane;
    int  last_click_idx;
    int  toolbar_hover;
    int  toolbar_pressed;
    int  dragging_splitter;
    int  dragging_scroll;
    int  scroll_drag_y;
    int  scroll_drag_top;
    int  cursor_shape;
};

static struct AppState G;
static int preview_text_line_count(void);

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

static int path_join_checked(char *out, const char *base, const char *name)
{
    int i = 0, j;
    if (base[0] == '/' && base[1] == 0) {
        if (i >= MAX_PATH - 1) return -1;
        out[i++] = '/';
    } else {
        for (j = 0; base[j] != 0; j++) {
            if (i >= MAX_PATH - 1) return -1;
            out[i++] = base[j];
        }
        if (i >= MAX_PATH - 1) return -1;
        out[i++] = '/';
    }
    for (j = 0; name[j] != 0; j++) {
        if (i >= MAX_PATH - 1) return -1;
        out[i++] = name[j];
    }
    out[i] = 0;
    return 0;
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

static int path_has_slash(const char *p)
{
    int i;
    for (i = 0; p[i] != 0; i++)
        if (p[i] == '/') return 1;
    return 0;
}

static char upper_char(char c)
{
    if ('a' <= c && c <= 'z') return c - 0x20;
    return c;
}

static int valid_83_char(char c)
{
    c = upper_char(c);
    if ('A' <= c && c <= 'Z') return 1;
    if ('0' <= c && c <= '9') return 1;
    if (c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' ||
            c == '@' || c == '~' || c == '`' || c == '!' || c == '(' ||
            c == ')' || c == '{' || c == '}' || c == '^' || c == '#' ||
            c == '&') return 1;
    return 0;
}

static int normalize_83(char *out, const char *in, int max)
{
    int i, j = 0, base = 0, ext = 0, dot = 0;
    if (in[0] == 0) return -1;
    if ((in[0] == '.' && in[1] == 0) ||
            (in[0] == '.' && in[1] == '.' && in[2] == 0)) return -1;
    for (i = 0; in[i] != 0; i++) {
        char c = upper_char(in[i]);
        if (c == '.' && !dot) {
            if (base <= 0) return -1;
            dot = 1;
            if (j < max - 1) out[j++] = '.';
            continue;
        }
        if (c == '.' || !valid_83_char(c)) return -1;
        if (dot) {
            ext++;
            if (ext > 3) return -1;
        } else {
            base++;
            if (base > 8) return -1;
        }
        if (j >= max - 1) return -1;
        out[j++] = c;
    }
    if (base <= 0) return -1;
    if (dot && ext <= 0) return -1;
    out[j] = 0;
    return 0;
}

static void path_leaf(char *out, const char *path, int max)
{
    const char *leaf = path_basename(path);
    s_cpy(out, leaf, max);
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

static int has_he2_ext(const char *name)
{
    int n = s_len(name);
    if (n < 4) return 0;
    return name[n - 4] == '.' && name[n - 3] == 'H' &&
            name[n - 2] == 'E' && name[n - 1] == '2';
}

static void append_text(char *dst, int max, const char *src)
{
    int i = s_len(dst), j;
    for (j = 0; src[j] != 0 && i < max - 1; j++) dst[i++] = src[j];
    dst[i] = 0;
}

static char hex_digit(int v)
{
    v &= 15;
    return (v < 10) ? ('0' + v) : ('A' + v - 10);
}

static int is_binary_preview(const char *buf, int len)
{
    int i, ctrl = 0;
    if (len == 0) return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char) buf[i];
        if (c == 0) return 1;
        if (c < 32 && c != '\n' && c != '\r' && c != '\t') ctrl++;
    }
    return ctrl > len / 20;
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

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void layout_compute(int cw, int ch, int tree_ratio, struct Layout *out)
{
    int middle_h = ch - TOOLBAR_H - STATUS_H;
    int avail = cw - SPLIT_W;
    int max_tree;
    int tree_w;

    if (middle_h < ROW_H * 2) middle_h = ROW_H * 2;
    if (avail < TREE_W_MIN + LIST_W_MIN) avail = TREE_W_MIN + LIST_W_MIN;

    max_tree = avail - LIST_W_MIN;
    if (max_tree < TREE_W_MIN) max_tree = TREE_W_MIN;
    tree_w = avail * tree_ratio / 1000;
    tree_w = clamp_int(tree_w, TREE_W_MIN, max_tree);

    out->toolbar.x = 0;
    out->toolbar.y = 0;
    out->toolbar.w = cw;
    out->toolbar.h = TOOLBAR_H;

    out->middle.x = 0;
    out->middle.y = TOOLBAR_H;
    out->middle.w = cw;
    out->middle.h = middle_h;

    out->tree.x = 0;
    out->tree.y = TOOLBAR_H;
    out->tree.w = tree_w;
    out->tree.h = middle_h;

    out->splitter.x = tree_w;
    out->splitter.y = TOOLBAR_H;
    out->splitter.w = SPLIT_W;
    out->splitter.h = middle_h;

    out->list.x = tree_w + SPLIT_W;
    out->list.y = TOOLBAR_H;
    out->list.w = cw - out->list.x;
    out->list.h = middle_h;
    if (out->list.w < LIST_W_MIN) out->list.w = LIST_W_MIN;

    out->status.x = 0;
    out->status.y = ch - STATUS_H;
    out->status.w = cw;
    out->status.h = STATUS_H;
}

static void compute_layout(void)
{
    G.cw = G.w - FRAME_L - FRAME_R;
    G.ch = G.h - FRAME_T - FRAME_B;
    if (G.tree_ratio <= 0) G.tree_ratio = TREE_RATIO_DEF;
    layout_compute(G.cw, G.ch, G.tree_ratio, &G.layout);
    G.tree_w = G.layout.tree.w;
}

static void set_tree_width_from_mouse(int cx)
{
    int avail = G.cw - SPLIT_W;
    int max_tree = avail - LIST_W_MIN;
    if (max_tree < TREE_W_MIN) max_tree = TREE_W_MIN;
    G.tree_w = clamp_int(cx, TREE_W_MIN, max_tree);
    if (avail > 0) {
        G.tree_ratio = G.tree_w * 1000 / avail;
        G.tree_ratio = clamp_int(G.tree_ratio, 50, 950);
    }
    compute_layout();
}

static int mid_top(void)    { return G.layout.middle.y; }
static int mid_bot(void)    { return G.layout.middle.y + G.layout.middle.h; }

static void set_cursor_shape(int shape)
{
    if (G.cursor_shape != shape) {
        api_setcursor(shape);
        G.cursor_shape = shape;
    }
}

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

/* ── 3D 베벨 헬퍼 (Win95 / haribote make_textbox8 스타일) ─────────────────
 *   raised : top/left = white, bottom/right = dark gray
 *   sunken : top/left = dark gray, bottom/right = white
 *   2px outset/inset 두께. 내부는 호출자가 별도로 채우거나 fill=col 로 동시 채움.
 *   좌표는 모두 client-area 기준 (s_x/s_y 가 frame offset 적용).               */
static void bevel_raised(int x0, int y0, int x1, int y1, int fill)
{
    if (fill >= 0) cli_box(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
    /* 바깥 1px: light TL, dark BR */
    cli_box(x0,     y0,     x1,     y0,     COL_WHITE);
    cli_box(x0,     y0,     x0,     y1,     COL_WHITE);
    cli_box(x0,     y1,     x1,     y1,     COL_DGRAY);
    cli_box(x1,     y0,     x1,     y1,     COL_DGRAY);
}

static void bevel_sunken(int x0, int y0, int x1, int y1, int fill)
{
    if (fill >= 0) cli_box(x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
    /* 바깥 1px: dark TL, light BR */
    cli_box(x0,     y0,     x1,     y0,     COL_DGRAY);
    cli_box(x0,     y0,     x0,     y1,     COL_DGRAY);
    cli_box(x0,     y1,     x1,     y1,     COL_WHITE);
    cli_box(x1,     y0,     x1,     y1,     COL_WHITE);
}

/* 깊은 sunken (textbox 처럼 2겹). 안쪽 1px: 진한 검정/light gray */
static void bevel_sunken2(int x0, int y0, int x1, int y1, int fill)
{
    if (fill >= 0) cli_box(x0 + 2, y0 + 2, x1 - 2, y1 - 2, fill);
    /* 바깥 1px: dark gray TL, white BR */
    cli_box(x0,     y0,     x1,     y0,     COL_DGRAY);
    cli_box(x0,     y0,     x0,     y1,     COL_DGRAY);
    cli_box(x0,     y1,     x1,     y1,     COL_WHITE);
    cli_box(x1,     y0,     x1,     y1,     COL_WHITE);
    /* 안쪽 1px: black TL, light gray BR */
    cli_box(x0 + 1, y0 + 1, x1 - 1, y0 + 1, COL_BLACK);
    cli_box(x0 + 1, y0 + 1, x0 + 1, y1 - 1, COL_BLACK);
    cli_box(x0 + 1, y1 - 1, x1 - 1, y1 - 1, COL_LGRAY);
    cli_box(x1 - 1, y0 + 1, x1 - 1, y1 - 1, COL_LGRAY);
}


/* 화면 폭에 맞춰 문자열을 잘라 표시. col 갯수만큼 글자만 표시.
 * 실제 폭이 부족하면 마지막 3 글자를 "..." 으로 대체.                        */
static int trunc_to_cols(char *dst, const char *src, int cols, int dst_max)
{
    int i, sl = s_len(src);
    if (dst_max <= 0) return 0;
    if (cols > dst_max - 1) cols = dst_max - 1;
    if (cols <= 0) { dst[0] = 0; return 0; }
    if (sl <= cols) {
        for (i = 0; i < sl; i++) dst[i] = src[i];
        dst[i] = 0;
        return sl;
    }
    if (cols <= 3) {
        for (i = 0; i < cols; i++) dst[i] = '.';
        dst[i] = 0;
        return cols;
    }
    for (i = 0; i < cols - 3; i++) dst[i] = src[i];
    dst[cols - 3] = '.';
    dst[cols - 2] = '.';
    dst[cols - 1] = '.';
    dst[cols] = 0;
    return cols;
}

static int trunc_path_to_cols(char *dst, const char *src, int cols, int dst_max)
{
    int sl = s_len(src);
    int keep_head, keep_tail, i, j;
    if (dst_max <= 0) return 0;
    if (cols > dst_max - 1) cols = dst_max - 1;
    if (cols <= 0) { dst[0] = 0; return 0; }
    if (sl <= cols) {
        for (i = 0; i < sl; i++) dst[i] = src[i];
        dst[i] = 0;
        return sl;
    }
    if (cols <= 3) {
        for (i = 0; i < cols; i++) dst[i] = '.';
        dst[i] = 0;
        return cols;
    }
    keep_head = (cols - 3) / 2;
    keep_tail = cols - 3 - keep_head;
    j = 0;
    for (i = 0; i < keep_head; i++) dst[j++] = src[i];
    dst[j++] = '.';
    dst[j++] = '.';
    dst[j++] = '.';
    for (i = sl - keep_tail; i < sl; i++) dst[j++] = src[i];
    dst[j] = 0;
    return j;
}

static struct Rect tree_inner_rect(void)
{
    struct Rect r;
    int top = mid_top(), bot = mid_bot();
    r.x = G.layout.tree.x + 4;
    r.y = top + 4;
    r.w = G.layout.tree.w - 8;
    r.h = bot - top - 8;
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    return r;
}

static struct Rect list_inner_rect(void)
{
    struct Rect r;
    int top = mid_top(), bot = mid_bot();
    r.x = G.layout.list.x + 4;
    r.y = top + 4;
    r.w = G.layout.list.w - 8;
    r.h = bot - top - 8;
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    return r;
}

static int scroll_thumb_y(int top, int count, int rows, int track_y, int track_h, int *thumb_h)
{
    int range, movable, h;
    if (rows <= 0 || count <= rows) {
        *thumb_h = track_h;
        return track_y;
    }
    h = track_h * rows / count;
    if (h < SCROLL_MIN_THUMB) h = SCROLL_MIN_THUMB;
    if (h > track_h) h = track_h;
    range = count - rows;
    movable = track_h - h;
    *thumb_h = h;
    if (movable <= 0 || range <= 0) return track_y;
    return track_y + top * movable / range;
}

static int tree_visible_rows(void)
{
    struct Rect r = tree_inner_rect();
    return r.h / ROW_H;
}

static int list_visible_rows(void)
{
    struct Rect r = list_inner_rect();
    int rows = (r.h - ROW_H) / ROW_H;
    return rows < 0 ? 0 : rows;
}

static int preview_visible_rows(void)
{
    return list_visible_rows();
}

static int preview_total_lines(void)
{
    if (G.preview_binary) return (G.preview_len + 7) / 8;
    return preview_text_line_count();
}

static void ensure_tree_sel_visible(void)
{
    int rows = tree_visible_rows();
    int max_top = G.tree_count - rows;
    if (max_top < 0) max_top = 0;
    if (rows <= 0) {
        G.tree_top = 0;
        return;
    }
    if (G.tree_sel < G.tree_top) G.tree_top = G.tree_sel;
    if (G.tree_sel >= G.tree_top + rows) G.tree_top = G.tree_sel - rows + 1;
    G.tree_top = clamp_int(G.tree_top, 0, max_top);
}

static void ensure_file_sel_visible(void)
{
    int rows = list_visible_rows();
    int max_top = G.files_count - rows;
    if (max_top < 0) max_top = 0;
    if (rows <= 0) {
        G.files_top = 0;
        return;
    }
    if (G.files_sel < G.files_top) G.files_top = G.files_sel;
    if (G.files_sel >= G.files_top + rows) G.files_top = G.files_sel - rows + 1;
    G.files_top = clamp_int(G.files_top, 0, max_top);
}

static int scroll_top_from_y(int y, int drag_y, int drag_top,
        int count, int rows, int track_y, int track_h)
{
    int thumb_h, range, movable, dy, top;
    scroll_thumb_y(drag_top, count, rows, track_y, track_h, &thumb_h);
    range = count - rows;
    movable = track_h - thumb_h;
    if (range <= 0 || movable <= 0) return 0;
    dy = y - drag_y;
    top = drag_top + dy * range / movable;
    return clamp_int(top, 0, range);
}

static void scroll_track_bounds(int y0, int h, int *track_y, int *track_h)
{
    if (h > SCROLL_W * 3) {
        *track_y = y0 + SCROLL_W;
        *track_h = h - SCROLL_W * 2;
    } else {
        *track_y = y0 + 1;
        *track_h = h - 2;
    }
    if (*track_h < 1) *track_h = 1;
}

static void draw_scrollbar(int x0, int y0, int h, int top, int count, int rows)
{
    int thumb_h, thumb_y, track_y, track_h;
    if (rows <= 0 || count <= rows) return;
    cli_box(x0, y0, x0 + SCROLL_W - 1, y0 + h - 1, COL_CHROME);
    bevel_sunken(x0, y0, x0 + SCROLL_W - 1, y0 + h - 1, COL_CHROME);
    scroll_track_bounds(y0, h, &track_y, &track_h);
    if (h > SCROLL_W * 3) {
        bevel_raised(x0 + 1, y0 + 1, x0 + SCROLL_W - 2,
                y0 + SCROLL_W - 1, COL_CHROME);
        bevel_raised(x0 + 1, y0 + h - SCROLL_W, x0 + SCROLL_W - 2,
                y0 + h - 2, COL_CHROME);
    }
    thumb_y = scroll_thumb_y(top, count, rows, track_y, track_h, &thumb_h);
    bevel_raised(x0 + 1, thumb_y, x0 + SCROLL_W - 2,
            thumb_y + thumb_h - 1, COL_CHROME);
}

static int toolbar_button_enabled(int btn)
{
    if (btn < 0 || btn >= 6) return 0;
    if (G.mode == MODE_INPUT || G.mode == MODE_CONFIRM) return 0;
    if ((btn == 4 || btn == 5) && G.files_count <= 0) return 0;
    return 1;
}

/* ---- pane 그리기 ------------------------------------------------------ */

static void draw_toolbar(void)
{
    char tmp[64];
    char path[MAX_PATH];
    int n;
    int tw = G.layout.toolbar.w;
    /* toolbar 자체를 raised 베벨 띠로 (light gray 면 + 위/아래 라인) */
    cli_box(0, 0, tw - 1, TOOLBAR_H - 1, COL_CHROME);
    /* 위쪽 1px: 흰색, 아래쪽 1px: 어두운 회색 → 화면에서 살짝 떠 있는 느낌 */
    cli_box(0, 0, tw - 1, 0, COL_WHITE);
    cli_box(0, TOOLBAR_H - 1, tw - 1, TOOLBAR_H - 1, COL_DGRAY);
    /* 아래쪽 다시 흰선 1px → toolbar/middle 사이 separator (raised line) */
    cli_box(0, TOOLBAR_H, tw - 1, TOOLBAR_H, COL_WHITE);
    /* placeholder buttons (raised) */
    {
        int x = 4, w = 22, y0 = 3, y1 = TOOLBAR_H - 5;
        const char *labels[6] = { "<-", "..", "R", "N", "D", "M" };
        int j;
        for (j = 0; j < 6; j++) {
            int enabled = toolbar_button_enabled(j);
            int pressed = (G.toolbar_pressed == j && enabled);
            int hover = (G.toolbar_hover == j && enabled);
            if (pressed) bevel_sunken(x, y0, x + w - 1, y1, COL_CHROME);
            else         bevel_raised(x, y0, x + w - 1, y1, COL_CHROME);
            if (hover && !pressed) {
                cli_box(x + 2, y0 + 2, x + w - 3, y0 + 2, COL_WHITE);
                cli_box(x + 2, y1 - 2, x + w - 3, y1 - 2, COL_DGRAY);
            }
            cli_str(x + (w - (s_len(labels[j])) * 8) / 2,
                    y0 + 3, enabled ? COL_TEXT : COL_DGRAY,
                    s_len(labels[j]), labels[j]);
            x += w + 2;
        }
        /* path 영역: sunken textbox */
        {
            int px0 = x + 4, py0 = 3, px1 = tw - 6, py1 = TOOLBAR_H - 5;
            if (px1 < px0 + 16) px1 = px0 + 16;
            bevel_sunken(px0, py0, px1, py1, COL_BG);
            s_cpy(path,
                    G.tree_count > 0 ? G.tree[G.tree_sel].path : "/",
                    MAX_PATH);
            n = trunc_path_to_cols(tmp, path, (px1 - px0 - 4) / 8, sizeof tmp);
            cli_str(px0 + 4, py0 + 3, COL_TEXT, n, tmp);
        }
    }
}

static void draw_status(void)
{
    char tmp[64];
    int y0 = G.layout.status.y, y1 = G.layout.status.y + G.layout.status.h - 1;
    int sw = G.layout.status.w;
    int n;
    int col = G.status_err ? COL_ERR : COL_TEXT;
    /* 위쪽 separator (raised line) */
    cli_box(0, y0,     sw - 1, y0,     COL_DGRAY);
    cli_box(0, y0 + 1, sw - 1, y0 + 1, COL_WHITE);
    /* status panel: light gray 면 + 안쪽에 sunken text 영역 */
    cli_box(0, y0 + 2, sw - 1, y1, COL_CHROME);
    bevel_sunken(4, y0 + 3, sw - 5, y1 - 2, COL_BG);
    n = trunc_path_to_cols(tmp, G.status, (sw - 16) / 8, sizeof tmp);
    cli_str(8, y0 + 5, col, n, tmp);
}

static void draw_tree(void)
{
    int top = mid_top(), bot = mid_bot();
    int rows;
    int i, r;
    int max_cols;
    /* tree 패널 = sunken textbox 식 베벨. 안쪽 영역(2px inset)을 row 영역으로. */
    int tree_x0 = G.layout.tree.x;
    int tree_x1 = G.layout.tree.x + G.layout.tree.w - 1;
    int outer_x0 = tree_x0 + 2, outer_y0 = top + 2;
    int outer_x1 = tree_x1 - 2, outer_y1 = bot - 3;
    int inner_x0 = outer_x0 + 2, inner_y0 = outer_y0 + 2;
    int inner_x1 = outer_x1 - 2, inner_y1 = outer_y1 - 2;
    int row_w = inner_x1 - inner_x0 + 1;
    int row_x1;
    int need_scroll;

    rows = (inner_y1 - inner_y0 + 1) / ROW_H;
    need_scroll = (G.tree_count > rows && rows > 0);
    if (need_scroll) {
        row_w -= SCROLL_W + 2;
    }
    row_x1 = inner_x0 + row_w - 1;
    {
        int max_top = G.tree_count - rows;
        if (max_top < 0) max_top = 0;
        G.tree_top = clamp_int(G.tree_top, 0, max_top);
    }

    /* 바깥 chrome 면 + sunken 베벨 */
    cli_box(tree_x0, top, tree_x1, bot - 1, COL_CHROME);
    bevel_sunken2(outer_x0, outer_y0, outer_x1, outer_y1, COL_BG);
    if (need_scroll) {
        draw_scrollbar(inner_x1 - SCROLL_W + 1, inner_y0,
                inner_y1 - inner_y0 + 1, G.tree_top, G.tree_count, rows);
    }

    max_cols = row_w / 8;
    for (r = 0; r < rows; r++) {
        i = G.tree_top + r;
        if (i >= G.tree_count) break;
        {
            struct ExpNode *nd = &G.tree[i];
            int row_y = inner_y0 + r * ROW_H;
            int indent = nd->depth * TREE_INDENT;
            const char *bn = (nd->depth == 0) ? "/" : path_basename(nd->path);
            char tmp[40];
            int n;
            int avail = max_cols - (indent / 8) - 2;
            int sel = (i == G.tree_sel);
            int sel_col = (G.focus == FOCUS_TREE) ? COL_SEL_FOC : COL_SEL_NOF;
            int fg = sel ? COL_SEL_FG : COL_TEXT;
            if (sel) {
                cli_box(inner_x0, row_y,
                        row_x1, row_y + ROW_H - 1, sel_col);
            }
            /* expand marker (작은 + / - 박스 풍) */
            {
                char m[2]; m[1] = 0;
                m[0] = nd->expanded ? '-' : '+';
                cli_str(inner_x0 + 2 + indent, row_y + 1, fg, 1, m);
            }
            if (avail < 1) avail = 1;
            n = trunc_to_cols(tmp, bn, avail, sizeof tmp);
            cli_str(inner_x0 + 2 + indent + 12, row_y + 1, fg, n, tmp);
        }
    }
    /* splitter — raised vertical bar */
    {
        int sx0 = G.layout.splitter.x;
        int sx1 = G.layout.splitter.x + G.layout.splitter.w - 1;
        cli_box(sx0, top, sx1, bot - 1, COL_CHROME);
        cli_box(sx0,     top, sx0,     bot - 1, COL_WHITE);
        cli_box(sx1,     top, sx1,     bot - 1, COL_DGRAY);
    }
}

static void draw_list(void)
{
    int top = mid_top(), bot = mid_bot();
    int pane_x0 = G.layout.list.x;
    int pane_x1 = G.layout.list.x + G.layout.list.w - 1;
    /* sunken textbox 안쪽에 헤더 + row 들 */
    int outer_x0 = pane_x0 + 2, outer_y0 = top + 2;
    int outer_x1 = pane_x1 - 2, outer_y1 = bot - 3;
    int inner_x0 = outer_x0 + 2, inner_y0 = outer_y0 + 2;
    int inner_x1 = outer_x1 - 2, inner_y1 = outer_y1 - 2;
    int row_w = inner_x1 - inner_x0 + 1;
    int row_x1;
    int header_y = inner_y0;
    int header_h = ROW_H;
    int rows;
    int r, i;
    int size_col_x;
    int show_size;
    int name_cols;
    int need_scroll;

    /* 바깥 chrome + sunken 베벨 */
    cli_box(pane_x0, top, pane_x1, bot - 1, COL_CHROME);
    bevel_sunken2(outer_x0, outer_y0, outer_x1, outer_y1, COL_BG);

    rows = (inner_y1 - (header_y + header_h) + 1) / ROW_H;
    need_scroll = (G.files_count > rows && rows > 0);
    if (need_scroll) {
        row_w -= SCROLL_W + 2;
        inner_x1 -= SCROLL_W + 2;
    }
    row_x1 = inner_x0 + row_w - 1;

    show_size = (row_w >= 18 * 8);
    /* size 컬럼 위치: 우측 끝에서 8 글자 폭 (+ 4 px 패딩). */
    size_col_x = inner_x1 - 8 * 8 + 1;
    name_cols = show_size ? (size_col_x - (inner_x0 + 4) - 8) / 8
                          : (row_w - 8) / 8;
    if (name_cols < 1) name_cols = 1;
    if (name_cols > 12) name_cols = 12;

    /* 헤더 (raised) */
    bevel_raised(inner_x0, header_y, row_x1, header_y + header_h - 1, COL_CHROME);
    cli_str(inner_x0 + 4, header_y + 2, COL_TEXT, 4, "Name");
    if (show_size) {
        cli_str(size_col_x, header_y + 2, COL_TEXT, 4, "Size");
    }

    /* row 영역 */
    {
        int max_top = G.files_count - rows;
        if (max_top < 0) max_top = 0;
        G.files_top = clamp_int(G.files_top, 0, max_top);
    }

    for (r = 0; r < rows; r++) {
        i = G.files_top + r;
        if (i >= G.files_count) break;
        {
            struct BX_DIRINFO *ent = &G.files[i];
            int row_y = header_y + header_h + r * ROW_H;
            int sel = (i == G.files_sel);
            int sel_col = (G.focus == FOCUS_LIST) ? COL_SEL_FOC : COL_SEL_NOF;
            int isdir = (ent->attr & 0x10) != 0;
            int isexec = (!isdir && has_he2_ext(ent->name));
            int fg = sel ? COL_SEL_FG : (isdir ? COL_DIR :
                    (isexec ? COL_EXEC : COL_TEXT));
            char nm[16];
            int n;
            char sz[16];
            if (sel) {
                cli_box(inner_x0, row_y,
                        row_x1, row_y + ROW_H - 1, sel_col);
            }
            n = trunc_to_cols(nm, ent->name, name_cols, sizeof nm);
            cli_str(inner_x0 + 4, row_y + 1, fg, n, nm);
            if (isdir && show_size) {
                cli_str(size_col_x, row_y + 1, fg, 5, "<DIR>");
            } else if (!isdir && show_size) {
                int_to_str(ent->size, sz);
                n = trunc_to_cols(nm, sz, 8, sizeof nm);
                cli_str(size_col_x, row_y + 1, fg, n, nm);
            }
        }
    }
    if (need_scroll) {
        draw_scrollbar(outer_x1 - SCROLL_W - 1, header_y + header_h,
                inner_y1 - (header_y + header_h) + 1,
                G.files_top, G.files_count, rows);
    }
}

static int preview_line_pos(int target)
{
    int pos = 0, line = 0;
    while (pos < G.preview_len && line < target) {
        unsigned char c = (unsigned char) G.preview_buf[pos++];
        if (c == '\n') line++;
    }
    return pos;
}

static int preview_text_line(char *out, int max, int line_no)
{
    int pos = preview_line_pos(line_no);
    int n = 0;
    while (pos < G.preview_len && n < max - 1) {
        unsigned char c = (unsigned char) G.preview_buf[pos++];
        if (c == '\n') break;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c < 32) c = '.';
        out[n++] = (char) c;
    }
    out[n] = 0;
    return n;
}

static int preview_text_line_count(void)
{
    int pos, lines = 1;
    if (G.preview_len <= 0) return 0;
    for (pos = 0; pos < G.preview_len; pos++)
        if (G.preview_buf[pos] == '\n') lines++;
    return lines;
}

static void preview_hex_line(char *out, int max, int offset)
{
    int i, p = 0;
    if (max < 8) { out[0] = 0; return; }
    out[p++] = hex_digit(offset >> 12);
    out[p++] = hex_digit(offset >> 8);
    out[p++] = hex_digit(offset >> 4);
    out[p++] = hex_digit(offset);
    out[p++] = ':';
    out[p++] = ' ';
    for (i = 0; i < 8 && offset + i < G.preview_len && p < max - 4; i++) {
        unsigned char c = (unsigned char) G.preview_buf[offset + i];
        out[p++] = hex_digit(c >> 4);
        out[p++] = hex_digit(c);
        out[p++] = ' ';
    }
    while (i++ < 8 && p < max - 4) {
        out[p++] = ' ';
        out[p++] = ' ';
        out[p++] = ' ';
    }
    out[p++] = ' ';
    for (i = 0; i < 8 && offset + i < G.preview_len && p < max - 1; i++) {
        unsigned char c = (unsigned char) G.preview_buf[offset + i];
        out[p++] = (c >= 32 && c < 127) ? (char) c : '.';
    }
    out[p] = 0;
}

static void draw_preview(void)
{
    int top = mid_top(), bot = mid_bot();
    int pane_x0 = G.layout.list.x;
    int pane_x1 = G.layout.list.x + G.layout.list.w - 1;
    int outer_x0 = pane_x0 + 2, outer_y0 = top + 2;
    int outer_x1 = pane_x1 - 2, outer_y1 = bot - 3;
    int inner_x0 = outer_x0 + 2, inner_y0 = outer_y0 + 2;
    int inner_x1 = outer_x1 - 2, inner_y1 = outer_y1 - 2;
    int header_h = ROW_H;
    int rows = (inner_y1 - (inner_y0 + header_h) + 1) / ROW_H;
    int cols = (inner_x1 - inner_x0 - 8) / 8;
    int r, total;
    char line[80];
    char path[64];
    if (cols > 79) cols = 79;
    if (cols < 1) cols = 1;

    cli_box(pane_x0, top, pane_x1, bot - 1, COL_CHROME);
    bevel_sunken2(outer_x0, outer_y0, outer_x1, outer_y1, COL_BG);

    bevel_raised(inner_x0, inner_y0, inner_x1, inner_y0 + header_h - 1, COL_CHROME);
    trunc_path_to_cols(path, G.preview_path, (inner_x1 - inner_x0 - 72) / 8, sizeof path);
    s_cpy(line, G.preview_binary ? "Hex: " : "Text: ", sizeof line);
    append_text(line, sizeof line, path);
    cli_str(inner_x0 + 4, inner_y0 + 2, COL_TEXT,
            trunc_to_cols(path, line, cols, sizeof path), path);

    if (G.preview_binary) {
        total = (G.preview_len + 7) / 8;
        if (G.preview_top > total - rows) G.preview_top = total - rows;
        if (G.preview_top < 0) G.preview_top = 0;
        for (r = 0; r < rows; r++) {
            int line_no = G.preview_top + r;
            if (line_no >= total) break;
            preview_hex_line(line, sizeof line, line_no * 8);
            cli_str(inner_x0 + 4, inner_y0 + header_h + r * ROW_H + 1,
                    COL_TEXT, trunc_to_cols(path, line, cols, sizeof path), path);
        }
        draw_scrollbar(inner_x1 - SCROLL_W + 1, inner_y0 + header_h,
                inner_y1 - (inner_y0 + header_h) + 1, G.preview_top, total, rows);
    } else {
        total = preview_text_line_count();
        if (G.preview_top > total - rows) G.preview_top = total - rows;
        if (G.preview_top < 0) G.preview_top = 0;
        for (r = 0; r < rows; r++) {
            int line_no = G.preview_top + r;
            if (line_no >= total) break;
            preview_text_line(line, sizeof line, line_no);
            cli_str(inner_x0 + 4, inner_y0 + header_h + r * ROW_H + 1,
                    COL_TEXT, trunc_to_cols(path, line, cols, sizeof path), path);
        }
        draw_scrollbar(inner_x1 - SCROLL_W + 1, inner_y0 + header_h,
                inner_y1 - (inner_y0 + header_h) + 1, G.preview_top, total, rows);
    }
}

static void redraw_all(void)
{
    /* 외곽 frame 은 make_window8 가 이미 그림. client 영역 재칠.            */
    draw_toolbar();
    draw_tree();
    if (G.mode == MODE_PREVIEW) draw_preview();
    else                        draw_list();
    draw_status();
    cli_refresh(0, 0, G.cw, G.ch);
}

/* ---- 디렉터리 진입/이동 ------------------------------------------------ */

static void status_set(const char *s, int err)
{
    s_cpy(G.status, s, sizeof G.status);
    G.status_err = err ? 1 : 0;
    G.status_transient = err ? 0 : 1;
}

static void status_clear_transient(void)
{
    if (G.status_transient && G.mode == MODE_LIST) {
        G.status[0] = 0;
        G.status_err = 0;
        G.status_transient = 0;
    }
}

static void refresh_input_status(void)
{
    s_cpy(G.status, G.input_prompt, sizeof G.status);
    append_text(G.status, sizeof G.status, G.input_buf);
    append_text(G.status, sizeof G.status, "_");
    G.status_err = 0;
    G.status_transient = 0;
}

static void select_file_by_name(const char *name, int fallback)
{
    int i;
    if (name != 0 && name[0] != 0) {
        for (i = 0; i < G.files_count; i++) {
            if (s_eq(G.files[i].name, name)) {
                G.files_sel = i;
                ensure_file_sel_visible();
                return;
            }
        }
    }
    if (G.files_count <= 0) {
        G.files_sel = 0;
        G.files_top = 0;
        return;
    }
    G.files_sel = clamp_int(fallback, 0, G.files_count - 1);
    ensure_file_sel_visible();
}

static void reload_current_dir_select(const char *select_name, int fallback)
{
    char cur[MAX_PATH];
    int idx;
    s_cpy(cur, G.tree_count > 0 ? G.tree[G.tree_sel].path : "/", sizeof cur);
    tree_init(cur);
    idx = tree_find(cur);
    if (idx < 0) {
        tree_init("/");
        idx = tree_find("/");
    }
    if (idx < 0) idx = 0;
    G.tree_sel = idx;
    ensure_tree_sel_visible();
    files_load(G.tree[G.tree_sel].path);
    select_file_by_name(select_name, fallback);
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
    ensure_tree_sel_visible();
    update_files_for_selection();
}

static void leave_preview(void)
{
    G.mode = MODE_LIST;
    G.preview_len = 0;
    G.preview_top = 0;
    status_set("back to list", 0);
}

static void load_preview(const char *path, const struct BX_DIRINFO *ent)
{
    int h, n, want;
    char num[12];
    h = api_fopen((char *) path);
    if (h == 0) {
        status_set("preview open failed", 1);
        return;
    }
    G.preview_size = api_fsize(h, 0);
    want = G.preview_size;
    if (want > PREVIEW_MAX) want = PREVIEW_MAX;
    n = api_fread(G.preview_buf, want, h);
    api_fclose(h);
    if (n < 0) n = 0;
    G.preview_len = n;
    G.preview_buf[n] = 0;
    G.preview_trunc = (G.preview_size > n);
    G.preview_binary = is_binary_preview(G.preview_buf, n);
    G.preview_top = 0;
    G.mode = MODE_PREVIEW;
    s_cpy(G.preview_path, path, sizeof G.preview_path);
    s_cpy(G.status, G.preview_binary ? "binary preview: " : "preview: ",
            sizeof G.status);
    append_text(G.status, sizeof G.status, ent->name);
    append_text(G.status, sizeof G.status, " ");
    int_to_str((unsigned int) G.preview_size, num);
    append_text(G.status, sizeof G.status, num);
    append_text(G.status, sizeof G.status, "B");
    if (G.preview_binary) {
        append_text(G.status, sizeof G.status, " clus ");
        int_to_str(ent->clustno, num);
        append_text(G.status, sizeof G.status, num);
    }
    if (G.preview_trunc) append_text(G.status, sizeof G.status, " (truncated)");
    G.status_err = 0;
}

static void open_selected_file(void)
{
    struct BX_DIRINFO *e;
    char child[MAX_PATH];
    int r;
    if (G.files_count <= 0 || G.files_sel < 0 || G.files_sel >= G.files_count) {
        status_set("nothing selected", 1);
        return;
    }
    e = &G.files[G.files_sel];
    if (path_join_checked(child, G.tree[G.tree_sel].path, e->name) != 0) {
        status_set("path too long", 1);
        return;
    }
    if ((e->attr & 0x10) != 0) {
        enter_dir_path(child);
        return;
    }
    if (has_he2_ext(e->name)) {
        r = api_exec(child, 0);
        if (r == 0) {
            status_set("launched", 0);
        } else {
            status_set("launch failed", 1);
        }
        return;
    }
    load_preview(child, e);
}

static int selected_path(char *out, struct BX_DIRINFO **ent)
{
    if (G.files_count <= 0 || G.files_sel < 0 || G.files_sel >= G.files_count) {
        status_set("nothing selected", 1);
        return -1;
    }
    if (ent != 0) *ent = &G.files[G.files_sel];
    if (path_join_checked(out, G.tree[G.tree_sel].path,
                G.files[G.files_sel].name) != 0) {
        status_set("path too long", 1);
        return -1;
    }
    return 0;
}

static void cancel_action(void)
{
    G.mode = MODE_LIST;
    G.action = ACT_NONE;
    G.input_len = 0;
    G.input_buf[0] = 0;
    status_set("cancelled", 0);
}

static void start_input_action(int action, const char *prompt, const char *initial)
{
    G.mode = MODE_INPUT;
    G.action = action;
    G.input_len = 0;
    G.input_buf[0] = 0;
    G.input_prompt[0] = 0;
    s_cpy(G.input_prompt, prompt, sizeof G.input_prompt);
    if (initial != 0) {
        s_cpy(G.input_buf, initial, sizeof G.input_buf);
        G.input_len = s_len(G.input_buf);
    }
    G.last_click_pane = -1;
    refresh_input_status();
}

static void start_mkdir(void)
{
    if (G.tree_count <= 0) {
        status_set("no current directory", 1);
        return;
    }
    start_input_action(ACT_MKDIR, "New dir: ", "");
}

static void start_rename(void)
{
    struct BX_DIRINFO *e;
    if (selected_path(G.op_path, &e) != 0) return;
    G.op_attr = e->attr;
    G.op_sel = G.files_sel;
    s_cpy(G.op_name, e->name, sizeof G.op_name);
    start_input_action(ACT_RENAME, "Rename to: ", e->name);
}

static void start_delete(void)
{
    struct BX_DIRINFO *e;
    char msg[80];
    if (selected_path(G.op_path, &e) != 0) return;
    G.mode = MODE_CONFIRM;
    G.action = ACT_DELETE;
    G.op_attr = e->attr;
    G.op_sel = G.files_sel;
    s_cpy(G.op_name, e->name, sizeof G.op_name);
    s_cpy(msg, "Delete ", sizeof msg);
    append_text(msg, sizeof msg, e->name);
    append_text(msg, sizeof msg, "? y/n");
    status_set(msg, 1);
    G.last_click_pane = -1;
}

static void finish_mkdir(void)
{
    char name[13], path[MAX_PATH];
    if (normalize_83(name, G.input_buf, sizeof name) != 0 ||
            path_has_slash(G.input_buf)) {
        status_set("invalid 8.3 directory name", 1);
        return;
    }
    if (path_join_checked(path, G.tree[G.tree_sel].path, name) != 0) {
        status_set("path too long", 1);
        return;
    }
    if (api_mkdir(path) != 0) {
        status_set("mkdir failed", 1);
        return;
    }
    G.mode = MODE_LIST;
    G.action = ACT_NONE;
    reload_current_dir_select(name, G.files_sel);
    status_set("directory created", 0);
}

static void finish_rename(void)
{
    char leaf[13], newpath[MAX_PATH], newleaf[13];
    path_leaf(leaf, G.input_buf, sizeof leaf);
    if (normalize_83(newleaf, leaf, sizeof newleaf) != 0) {
        status_set("invalid 8.3 name", 1);
        return;
    }
    if (G.input_buf[0] == '/') {
        s_cpy(newpath, G.input_buf, sizeof newpath);
    } else if (path_has_slash(G.input_buf)) {
        if (path_join_checked(newpath, G.tree[G.tree_sel].path,
                    G.input_buf) != 0) {
            status_set("path too long", 1);
            return;
        }
    } else {
        if (path_join_checked(newpath, G.tree[G.tree_sel].path,
                    newleaf) != 0) {
            status_set("path too long", 1);
            return;
        }
    }
    if (api_rename(G.op_path, newpath) != 0) {
        status_set("rename failed", 1);
        return;
    }
    G.mode = MODE_LIST;
    G.action = ACT_NONE;
    reload_current_dir_select(newleaf, G.op_sel);
    status_set("renamed", 0);
}

static void finish_delete(void)
{
    int r;
    if ((G.op_attr & 0x10) != 0) r = api_rmdir(G.op_path);
    else                         r = api_fdelete(G.op_path);
    if (r != 0) {
        status_set((G.op_attr & 0x10) != 0 ?
                "delete failed (dir not empty?)" : "delete failed", 1);
        G.mode = MODE_LIST;
        G.action = ACT_NONE;
        return;
    }
    G.mode = MODE_LIST;
    G.action = ACT_NONE;
    reload_current_dir_select(0, G.op_sel);
    status_set("deleted", 0);
}

static void finish_input_action(void)
{
    if (G.action == ACT_MKDIR) {
        finish_mkdir();
    } else if (G.action == ACT_RENAME) {
        finish_rename();
    }
}

static void on_input_key(int key)
{
    if (key == KEY_ESC) {
        cancel_action();
        return;
    }
    if (key == KEY_ENTER) {
        finish_input_action();
        return;
    }
    if (key == KEY_BS) {
        if (G.input_len > 0) {
            G.input_buf[--G.input_len] = 0;
            refresh_input_status();
        }
        return;
    }
    if (key >= 32 && key < 127 && G.input_len < MAX_PATH - 1) {
        G.input_buf[G.input_len++] = (char) key;
        G.input_buf[G.input_len] = 0;
        refresh_input_status();
    }
}

static void on_confirm_key(int key)
{
    if (key == 'y' || key == 'Y') {
        if (G.action == ACT_DELETE) finish_delete();
    } else if (key == 'n' || key == 'N' || key == KEY_ESC ||
            key == KEY_BS || key == KEY_ENTER) {
        cancel_action();
    }
}

/* ---- 입력 처리 ---------------------------------------------------------- */

static void on_key(int key)
{
    char parent[MAX_PATH];
    if (G.mode == MODE_INPUT) {
        on_input_key(key);
        return;
    }
    if (G.mode == MODE_CONFIRM) {
        on_confirm_key(key);
        return;
    }
    if (G.mode == MODE_PREVIEW) {
        if (key == 'q') {
            api_closewin(G.win);
            api_end();
        }
        if (key == KEY_ESC || key == KEY_BS || key == KEY_ENTER ||
                key == KEY_LEFT) {
            leave_preview();
            return;
        }
        if (key == KEY_UP && G.preview_top > 0) {
            G.preview_top--;
            return;
        }
        if (key == KEY_DOWN) {
            G.preview_top++;
            return;
        }
        return;
    }
    if (key == 'q' || key == KEY_ESC) {
        api_closewin(G.win);
        api_end();
    }
    status_clear_transient();
    if (key == KEY_TAB) {
        G.focus = (G.focus == FOCUS_TREE) ? FOCUS_LIST : FOCUS_TREE;
        return;
    }
    if (key == 'r') {
        update_files_for_selection();
        return;
    }
    if (key == 'n' || key == 'N') {
        start_mkdir();
        return;
    }
    if (key == 'd' || key == 'D') {
        start_delete();
        return;
    }
    if (key == 'm' || key == 'M') {
        start_rename();
        return;
    }
    if (G.focus == FOCUS_TREE) {
        if (key == KEY_UP) {
            if (G.tree_sel > 0) G.tree_sel--;
            ensure_tree_sel_visible();
            update_files_for_selection();
        } else if (key == KEY_DOWN) {
            if (G.tree_sel + 1 < G.tree_count) G.tree_sel++;
            ensure_tree_sel_visible();
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
                ensure_tree_sel_visible();
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
            ensure_file_sel_visible();
        } else if (key == KEY_DOWN) {
            if (G.files_sel + 1 < G.files_count) G.files_sel++;
            ensure_file_sel_visible();
        } else if (key == KEY_LEFT || key == KEY_BS) {
            /* 부모로 */
            path_parent(G.tree[G.tree_sel].path, parent);
            enter_dir_path(parent);
        } else if (key == KEY_ENTER || key == KEY_RIGHT) {
            open_selected_file();
        }
    }
}

static int hit_pane(int cx, int cy)
{
    int top = mid_top(), bot = mid_bot();
    if (cx < 0 || cy < top || cy >= bot) return -1;
    if (cx >= G.layout.splitter.x &&
            cx < G.layout.splitter.x + G.layout.splitter.w) return HIT_SPLITTER;
    if (cx >= G.layout.tree.x &&
            cx < G.layout.tree.x + G.layout.tree.w) return FOCUS_TREE;
    if (cx >= G.layout.list.x &&
            cx < G.layout.list.x + G.layout.list.w) return FOCUS_LIST;
    return -1;
}

static int tree_row_at(int cy)
{
    int top = mid_top() + 4;
    int bot = mid_bot() - 5;
    if (cy < top || cy > bot) return -1;
    return (cy - top) / ROW_H;
}

static int list_row_at(int cy)
{
    int top = mid_top() + 4 + ROW_H; /* skip header */
    int bot = mid_bot() - 5;
    if (cy < top || cy > bot) return -1;
    return (cy - top) / ROW_H;
}

static int hit_tree_scroll(int cx, int cy)
{
    struct Rect r = tree_inner_rect();
    int rows = r.h / ROW_H;
    int sx0 = r.x + r.w - SCROLL_W;
    if (rows <= 0 || G.tree_count <= rows) return 0;
    return (sx0 <= cx && cx < sx0 + SCROLL_W && r.y <= cy && cy < r.y + r.h);
}

static int hit_list_scroll(int cx, int cy)
{
    struct Rect r = list_inner_rect();
    int rows = (r.h - ROW_H) / ROW_H;
    int sx0 = r.x + r.w - SCROLL_W;
    int sy0 = r.y + ROW_H;
    if (rows <= 0 || G.files_count <= rows) return 0;
    return (sx0 <= cx && cx < sx0 + SCROLL_W && sy0 <= cy && cy < r.y + r.h);
}

static int hit_preview_scroll(int cx, int cy)
{
    struct Rect r = list_inner_rect();
    int rows = preview_visible_rows();
    int total = preview_total_lines();
    int sx0 = r.x + r.w - SCROLL_W;
    int sy0 = r.y + ROW_H;
    if (rows <= 0 || total <= rows) return 0;
    return (sx0 <= cx && cx < sx0 + SCROLL_W && sy0 <= cy && cy < r.y + r.h);
}

static void begin_scroll_drag(int pane, int cy)
{
    struct Rect r;
    int rows, count, top, track_y, track_h, thumb_h, thumb_y;
    if (pane == SCROLL_TREE) {
        r = tree_inner_rect();
        rows = r.h / ROW_H;
        count = G.tree_count;
        top = G.tree_top;
        scroll_track_bounds(r.y, r.h, &track_y, &track_h);
    } else {
        r = list_inner_rect();
        rows = (r.h - ROW_H) / ROW_H;
        if (pane == SCROLL_PREVIEW) {
            count = preview_total_lines();
            top = G.preview_top;
        } else {
            count = G.files_count;
            top = G.files_top;
        }
        scroll_track_bounds(r.y + ROW_H, r.h - ROW_H, &track_y, &track_h);
    }
    if (rows <= 0 || count <= rows) return;
    thumb_y = scroll_thumb_y(top, count, rows, track_y, track_h, &thumb_h);
    if (pane == SCROLL_TREE && r.h > SCROLL_W * 3 && cy < r.y + SCROLL_W) {
        top--;
    } else if (pane == SCROLL_TREE && r.h > SCROLL_W * 3 &&
            cy >= r.y + r.h - SCROLL_W) {
        top++;
    } else if (pane != SCROLL_TREE && r.h - ROW_H > SCROLL_W * 3 &&
            cy < r.y + ROW_H + SCROLL_W) {
        top--;
    } else if (pane != SCROLL_TREE && r.h - ROW_H > SCROLL_W * 3 &&
            cy >= r.y + r.h - SCROLL_W) {
        top++;
    } else if (cy < thumb_y) {
        top -= rows;
    } else if (cy >= thumb_y + thumb_h) {
        top += rows;
    } else {
        G.dragging_scroll = pane;
        G.scroll_drag_y = cy;
        G.scroll_drag_top = top;
        return;
    }
    top = clamp_int(top, 0, count - rows);
    if (pane == SCROLL_TREE) G.tree_top = top;
    else if (pane == SCROLL_PREVIEW) G.preview_top = top;
    else                             G.files_top = top;
}

static void update_scroll_drag(int cy)
{
    struct Rect r;
    int rows, count, track_y, track_h, top;
    if (G.dragging_scroll == SCROLL_TREE) {
        r = tree_inner_rect();
        rows = r.h / ROW_H;
        count = G.tree_count;
        scroll_track_bounds(r.y, r.h, &track_y, &track_h);
        top = scroll_top_from_y(cy, G.scroll_drag_y, G.scroll_drag_top,
                count, rows, track_y, track_h);
        G.tree_top = top;
    } else if (G.dragging_scroll == SCROLL_LIST) {
        r = list_inner_rect();
        rows = (r.h - ROW_H) / ROW_H;
        count = G.files_count;
        scroll_track_bounds(r.y + ROW_H, r.h - ROW_H, &track_y, &track_h);
        top = scroll_top_from_y(cy, G.scroll_drag_y, G.scroll_drag_top,
                count, rows, track_y, track_h);
        G.files_top = top;
    } else if (G.dragging_scroll == SCROLL_PREVIEW) {
        r = list_inner_rect();
        rows = (r.h - ROW_H) / ROW_H;
        count = preview_total_lines();
        scroll_track_bounds(r.y + ROW_H, r.h - ROW_H, &track_y, &track_h);
        top = scroll_top_from_y(cy, G.scroll_drag_y, G.scroll_drag_top,
                count, rows, track_y, track_h);
        G.preview_top = top;
    }
}

static int toolbar_button_at(int cx, int cy)
{
    int x = 4, w = 22, gap = 2, j;
    if (cy < 3 || cy > TOOLBAR_H - 5) return -1;
    for (j = 0; j < 6; j++) {
        if (cx >= x && cx <= x + w - 1) return j;
        x += w + gap;
    }
    return -1;
}

static void activate_toolbar_button(int btn)
{
    char parent[MAX_PATH];
    if (!toolbar_button_enabled(btn)) return;
    if (G.mode == MODE_INPUT || G.mode == MODE_CONFIRM) return;
    if (G.mode == MODE_PREVIEW) {
        leave_preview();
        if (btn < 3) return;
    }
    if (btn == 0 || btn == 1) {
        path_parent(G.tree[G.tree_sel].path, parent);
        enter_dir_path(parent);
    } else if (btn == 2) {
        reload_current_dir_select(0, G.files_sel);
        status_set("refreshed", 0);
    } else if (btn == 3) {
        start_mkdir();
    } else if (btn == 4) {
        start_delete();
    } else if (btn == 5) {
        start_rename();
    }
}

static void on_mouse_down(int cx, int cy)
{
    int pane = hit_pane(cx, cy);
    status_clear_transient();
    if (cy >= 0 && cy < TOOLBAR_H) {
        int btn = toolbar_button_at(cx, cy);
        G.dragging_splitter = 0;
        G.dragging_scroll = SCROLL_NONE;
        G.last_click_pane = -1;
        G.toolbar_pressed = toolbar_button_enabled(btn) ? btn : -1;
        if (btn >= 0) activate_toolbar_button(btn);
        return;
    }
    if (G.mode == MODE_INPUT || G.mode == MODE_CONFIRM) {
        G.dragging_splitter = 0;
        G.dragging_scroll = SCROLL_NONE;
        G.last_click_pane = -1;
        return;
    }
    if (G.mode == MODE_PREVIEW) {
        G.dragging_splitter = 0;
        G.dragging_scroll = SCROLL_NONE;
        G.last_click_pane = -1;
        if (pane == HIT_SPLITTER) {
            G.dragging_splitter = 1;
            set_cursor_shape(BX_CURSOR_RESIZE_WE);
            set_tree_width_from_mouse(cx);
        } else if (pane == FOCUS_LIST && hit_preview_scroll(cx, cy)) {
            begin_scroll_drag(SCROLL_PREVIEW, cy);
        }
        return;
    }
    if (pane == HIT_SPLITTER) {
        G.dragging_splitter = 1;
        G.dragging_scroll = SCROLL_NONE;
        G.last_click_pane = -1;
        set_cursor_shape(BX_CURSOR_RESIZE_WE);
        set_tree_width_from_mouse(cx);
    } else if (pane == FOCUS_TREE) {
        int row = tree_row_at(cy);
        int idx = G.tree_top + row;
        G.dragging_splitter = 0;
        if (hit_tree_scroll(cx, cy)) {
            G.dragging_scroll = SCROLL_NONE;
            begin_scroll_drag(SCROLL_TREE, cy);
            return;
        }
        G.dragging_scroll = SCROLL_NONE;
        G.focus = FOCUS_TREE;
        if (row >= 0 && idx >= 0 && idx < G.tree_count) {
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
        int row = list_row_at(cy);
        int idx;
        G.dragging_splitter = 0;
        if (hit_list_scroll(cx, cy)) {
            G.dragging_scroll = SCROLL_NONE;
            begin_scroll_drag(SCROLL_LIST, cy);
            return;
        }
        G.dragging_scroll = SCROLL_NONE;
        G.focus = FOCUS_LIST;
        if (row < 0) return;
        idx = G.files_top + row;
        if (idx >= 0 && idx < G.files_count) {
            int dbl = (G.last_click_pane == FOCUS_LIST && G.last_click_idx == idx);
            G.files_sel = idx;
            if (dbl) {
                open_selected_file();
                G.last_click_pane = -1;
            } else {
                G.last_click_pane = FOCUS_LIST;
                G.last_click_idx = idx;
            }
        }
    } else {
        G.dragging_splitter = 0;
        G.dragging_scroll = SCROLL_NONE;
        G.last_click_pane = -1;
    }
}

/* mouse_move 처리. 시각적 변화가 있을 때 1, 없으면 0 — 호출자가 redraw_all skip
 * 결정에 사용. mouse_move 는 가장 자주 발생하는 이벤트라 hover/drag 변화 없을
 * 때 redraw_all 안 부르는 게 큰 절감 (work6 Phase 7). */
static int on_mouse_move(int cx, int cy, int button)
{
    int prev_hover = G.toolbar_hover;
    G.toolbar_hover = toolbar_button_at(cx, cy);
    if (G.dragging_splitter) {
        set_cursor_shape(BX_CURSOR_RESIZE_WE);
        if ((button & 1) != 0) {
            set_tree_width_from_mouse(cx);
        } else {
            G.dragging_splitter = 0;
        }
        return 1;
    } else if (G.dragging_scroll != SCROLL_NONE) {
        if ((button & 1) != 0) {
            update_scroll_drag(cy);
        } else {
            G.dragging_scroll = SCROLL_NONE;
        }
        return 1;
    } else {
        set_cursor_shape(hit_pane(cx, cy) == HIT_SPLITTER ?
                BX_CURSOR_RESIZE_WE : BX_CURSOR_ARROW);
    }
    return G.toolbar_hover != prev_hover;
}

static void on_mouse_up(int cx, int cy)
{
    (void) cy;
    if (G.dragging_splitter) {
        set_tree_width_from_mouse(cx);
        G.dragging_splitter = 0;
    }
    G.toolbar_pressed = -1;
    G.dragging_scroll = SCROLL_NONE;
}

static void on_resize(int new_w, int new_h)
{
    int nw = new_w, nh = new_h;
    char *new_buf;
    int new_size;
    G.dragging_splitter = 0;
    G.dragging_scroll = SCROLL_NONE;
    if (nw < WIN_W_MIN) nw = WIN_W_MIN;
    if (nh < WIN_H_MIN) nh = WIN_H_MIN;
    new_size = nw * nh;
    new_buf = api_malloc(new_size);
    if (new_buf == 0) {
        status_set("resize failed (oom)", 1);
        return;
    }
    {
        char *old_buf = G.buf;
        int old_size = G.buf_size;
        if (api_resizewin(G.win, new_buf, nw, nh, -1) != 0) {
            api_free(new_buf, new_size);
            status_set("resize failed", 1);
            return;
        }
        G.w = nw;
        G.h = nh;
        G.buf = new_buf;
        G.buf_size = new_size;
        compute_layout();
        api_free(old_buf, old_size);
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
    G.tree_ratio = TREE_RATIO_DEF;
    G.focus = FOCUS_TREE;
    G.status_transient = 0;
    G.mode = MODE_LIST;
    G.preview_path[0] = 0;
    G.preview_len = 0;
    G.preview_size = 0;
    G.preview_trunc = 0;
    G.preview_binary = 0;
    G.preview_top = 0;
    G.action = ACT_NONE;
    G.input_prompt[0] = 0;
    G.input_buf[0] = 0;
    G.input_len = 0;
    G.op_path[0] = 0;
    G.op_name[0] = 0;
    G.op_attr = 0;
    G.op_sel = 0;
    G.last_click_pane = -1;
    G.last_click_idx = -1;
    G.toolbar_hover = -1;
    G.toolbar_pressed = -1;
    G.dragging_splitter = 0;
    G.dragging_scroll = SCROLL_NONE;
    G.cursor_shape = BX_CURSOR_ARROW;
    G.buf_size = G.w * G.h;
    G.buf = api_malloc(G.buf_size);
    G.win = api_openwin(G.buf, G.w, G.h, -1, "Explorer");
    api_set_winevent(G.win, BX_WIN_EV_MOUSE | BX_WIN_EV_RESIZE);

    compute_layout();
    tree_init(start_path);
    update_files_for_selection();
    redraw_all();

    for (;;) {
        int needs_redraw = 1;
        if (api_getevent(&ev, 1) != 1) continue;
        if (ev.type == BX_EVENT_KEY) {
            on_key(ev.key);
        } else if (ev.type == BX_EVENT_MOUSE_DOWN) {
            if (ev.button & 1) on_mouse_down(ev.x, ev.y);
        } else if (ev.type == BX_EVENT_MOUSE_MOVE) {
            needs_redraw = on_mouse_move(ev.x, ev.y, ev.button);
        } else if (ev.type == BX_EVENT_MOUSE_UP) {
            on_mouse_up(ev.x, ev.y);
        } else if (ev.type == BX_EVENT_RESIZE) {
            on_resize(ev.w, ev.h);
        }
        if (needs_redraw) redraw_all();
    }
}
