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
 *   splitter drag 로 tree/list 폭 조절.
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

    int  focus;
    char status[80];
    int  status_err;

    /* 같은-row 두 번 click → open. Mouse pane: 0=tree,1=list,-1=none */
    int  last_click_pane;
    int  last_click_idx;
    int  dragging_splitter;
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
            bevel_raised(x, y0, x + w - 1, y1, COL_CHROME);
            cli_str(x + (w - (s_len(labels[j])) * 8) / 2,
                    y0 + 3, COL_TEXT,
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

    rows = (inner_y1 - inner_y0 + 1) / ROW_H;
    /* clamp top */
    if (G.tree_sel < G.tree_top) G.tree_top = G.tree_sel;
    if (G.tree_sel >= G.tree_top + rows) G.tree_top = G.tree_sel - rows + 1;
    if (G.tree_top < 0) G.tree_top = 0;

    /* 바깥 chrome 면 + sunken 베벨 */
    cli_box(tree_x0, top, tree_x1, bot - 1, COL_CHROME);
    bevel_sunken2(outer_x0, outer_y0, outer_x1, outer_y1, COL_BG);

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
                        inner_x1, row_y + ROW_H - 1, sel_col);
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
    int header_y = inner_y0;
    int header_h = ROW_H;
    int rows;
    int r, i;
    int size_col_x;
    int show_size;
    int name_cols;

    /* 바깥 chrome + sunken 베벨 */
    cli_box(pane_x0, top, pane_x1, bot - 1, COL_CHROME);
    bevel_sunken2(outer_x0, outer_y0, outer_x1, outer_y1, COL_BG);

    show_size = (row_w >= 18 * 8);
    /* size 컬럼 위치: 우측 끝에서 8 글자 폭 (+ 4 px 패딩). */
    size_col_x = inner_x1 - 8 * 8 + 1;
    name_cols = show_size ? (size_col_x - (inner_x0 + 4) - 8) / 8
                          : (row_w - 8) / 8;
    if (name_cols < 1) name_cols = 1;
    if (name_cols > 12) name_cols = 12;

    /* 헤더 (raised) */
    bevel_raised(inner_x0, header_y, inner_x1, header_y + header_h - 1, COL_CHROME);
    cli_str(inner_x0 + 4, header_y + 2, COL_TEXT, 4, "Name");
    if (show_size) {
        cli_str(size_col_x, header_y + 2, COL_TEXT, 4, "Size");
    }

    /* row 영역 */
    rows = (inner_y1 - (header_y + header_h) + 1) / ROW_H;
    if (G.files_sel < G.files_top) G.files_top = G.files_sel;
    if (G.files_sel >= G.files_top + rows) G.files_top = G.files_sel - rows + 1;
    if (G.files_top < 0) G.files_top = 0;

    for (r = 0; r < rows; r++) {
        i = G.files_top + r;
        if (i >= G.files_count) break;
        {
            struct BX_DIRINFO *ent = &G.files[i];
            int row_y = header_y + header_h + r * ROW_H;
            int sel = (i == G.files_sel);
            int sel_col = (G.focus == FOCUS_LIST) ? COL_SEL_FOC : COL_SEL_NOF;
            int isdir = (ent->attr & 0x10) != 0;
            int fg = sel ? COL_SEL_FG : (isdir ? COL_DIR : COL_TEXT);
            char nm[16];
            int n;
            char sz[16];
            if (sel) {
                cli_box(inner_x0, row_y,
                        inner_x1, row_y + ROW_H - 1, sel_col);
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

static void on_mouse_down(int cx, int cy)
{
    int pane = hit_pane(cx, cy);
    if (pane == HIT_SPLITTER) {
        G.dragging_splitter = 1;
        G.last_click_pane = -1;
        set_tree_width_from_mouse(cx);
    } else if (pane == FOCUS_TREE) {
        int row = tree_row_at(cy);
        int idx = G.tree_top + row;
        G.dragging_splitter = 0;
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
        G.focus = FOCUS_LIST;
        if (row < 0) return;
        idx = G.files_top + row;
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
        G.dragging_splitter = 0;
        G.last_click_pane = -1;
    }
}

static void on_mouse_move(int cx, int cy, int button)
{
    (void) cy;
    if (G.dragging_splitter) {
        if ((button & 1) != 0) {
            set_tree_width_from_mouse(cx);
        } else {
            G.dragging_splitter = 0;
        }
    }
}

static void on_mouse_up(int cx, int cy)
{
    (void) cy;
    if (G.dragging_splitter) {
        set_tree_width_from_mouse(cx);
        G.dragging_splitter = 0;
    }
}

static void on_resize(int new_w, int new_h)
{
    int nw = new_w, nh = new_h;
    char *new_buf;
    int new_size;
    G.dragging_splitter = 0;
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
    G.last_click_pane = -1;
    G.last_click_idx = -1;
    G.dragging_splitter = 0;
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
        } else if (ev.type == BX_EVENT_MOUSE_MOVE) {
            on_mouse_move(ev.x, ev.y, ev.button);
        } else if (ev.type == BX_EVENT_MOUSE_UP) {
            on_mouse_up(ev.x, ev.y);
        } else if (ev.type == BX_EVENT_RESIZE) {
            on_resize(ev.w, ev.h);
        }
        redraw_all();
    }
}
