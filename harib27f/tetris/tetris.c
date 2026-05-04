/* tetris.c — BxOS sample. invader.c 패턴 참고.
 *
 * 조작:
 *   ← / → : 좌우 이동
 *   ↑     : 회전 (시계방향)
 *   ↓     : 소프트 드롭
 *   space : 하드 드롭
 *   Enter : 게임 오버 후 재시작
 */

#include "apilib.h"

/* 커널의 CONS_KEY_* (bootpack.h) 와 동일. 헤더가 없으므로 직접 정의. */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83

/* api_inittimer 데이터 값. CONS_KEY_* (0x80~0x83) 및 커서 타이머값(0,1)
 * 과 충돌하면 안 된다. invader 는 128(=0x80) 을 썼지만 그건 ↑ 키와 동일
 * 하다 — 화살표를 쓰는 본 앱에서는 다른 값을 사용한다. */
#define TIMER_SIG   0xfa

#define FW          10              /* field width  (cells) */
#define FH          20              /* field height (cells) */
#define CELL        12              /* cell size in pixels */
#define FX0         8               /* field origin x (in window) */
#define FY0         30              /* field origin y (in window) */
#define BOARD_W     (FW * CELL)
#define BOARD_H     (FH * CELL)
#define WIN_W       (FX0 + BOARD_W + 70)     /* = 198 */
#define WIN_H       (FY0 + BOARD_H + 12)     /* = 282 */

static unsigned char field[FH][FW];

/* work6 마무리: line-clear 후 보드 전체 redraw 시 cell × 200 boxfilwin 대신
 * 사용자 buffer 에 직접 채우고 api_blit_rect 1회로 옮김 (tetris_t 측정 결과
 * 적용). piece 움직임 (cell 4개 erase + 4개 draw) 은 boxfilwin 그대로 — 작은
 * rect 4~8개를 매 frame 28 KB memcpy 로 바꾸면 오히려 회귀. */
static unsigned char board_buf[BOARD_W * BOARD_H];

/* 7 pieces × 4 rotations × 4 cells × (dx,dy) */
static const signed char pieces[7][4][4][2] = {
    /* I */
    { {{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}},
      {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}} },
    /* O */
    { {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}},
      {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}} },
    /* T */
    { {{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}},
      {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}} },
    /* S */
    { {{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}},
      {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}} },
    /* Z */
    { {{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}},
      {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}} },
    /* L */
    { {{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}},
      {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}} },
    /* J */
    { {{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}},
      {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}} },
};

/* 밝은 16색 팔레트 (graphic.c table_rgb):
 *   1=빨강 2=초록 3=노랑 4=파랑 5=마젠타 6=시안 7=흰색 8=밝은회색
 * 어두운 9~14 는 가독성이 떨어져 사용 안 함.
 * L 의 오렌지는 216색 팔레트에서 가져온다 (16 + r + g*6 + b*36):
 *   r=5,g=2,b=0 → 16+5+12 = 33   (RGB ≈ 255,102,0)
 */
static const unsigned char piece_color[7] = {
    6,   /* I : cyan          */
    3,   /* O : yellow        */
    5,   /* T : magenta       */
    2,   /* S : green         */
    1,   /* Z : red           */
    33,  /* L : orange (216팔, RGB 255,102,0)  */
    216, /* J : blue   (216팔, RGB 102,153,255) */
};

static unsigned int rng_state = 0x12345678;

static int rng7(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) & 0x7fff) % 7;
}

static int collides(int type, int rot, int x, int y)
{
    int i, cx, cy;
    for (i = 0; i < 4; i++) {
        cx = x + pieces[type][rot][i][0];
        cy = y + pieces[type][rot][i][1];
        if (cx < 0 || cx >= FW || cy >= FH) return 1;
        if (cy < 0) continue;
        if (field[cy][cx] != 0) return 1;
    }
    return 0;
}

static void cell_rect(int cx, int cy, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = FX0 + cx * CELL;
    *y0 = FY0 + cy * CELL;
    *x1 = *x0 + CELL - 1;
    *y1 = *y0 + CELL - 1;
}

static void draw_cell(int win, int cx, int cy, int col)
{
    int x0, y0, x1, y1;
    cell_rect(cx, cy, &x0, &y0, &x1, &y1);
    /* win+1 → 즉시 화면 갱신 X (refresh 는 한 번에) */
    api_boxfilwin(win + 1, x0, y0, x1, y1, col);
    if (col != 0) {
        /* 셀 경계선 */
        api_boxfilwin(win + 1, x0, y0, x1, y0, 0);
        api_boxfilwin(win + 1, x0, y1, x1, y1, 0);
        api_boxfilwin(win + 1, x0, y0, x0, y1, 0);
        api_boxfilwin(win + 1, x1, y0, x1, y1, 0);
    }
}

static void draw_piece(int win, int type, int rot, int x, int y, int col)
{
    int i, cx, cy;
    for (i = 0; i < 4; i++) {
        cx = x + pieces[type][rot][i][0];
        cy = y + pieces[type][rot][i][1];
        if (cy < 0 || cy >= FH || cx < 0 || cx >= FW) continue;
        draw_cell(win, cx, cy, col);
    }
}

/* board_buf 안의 한 cell 영역에 색 + (col != 0 일 때) 검정 테두리 채움. */
static void board_buf_cell(int cx, int cy, int col)
{
    int x0 = cx * CELL, y0 = cy * CELL;
    int x, y;
    unsigned char c = (unsigned char) col;
    for (y = 0; y < CELL; y++) {
        unsigned char *row = board_buf + (y0 + y) * BOARD_W + x0;
        int border_y = (col != 0) && (y == 0 || y == CELL - 1);
        for (x = 0; x < CELL; x++) {
            int border_x = (col != 0) && (x == 0 || x == CELL - 1);
            row[x] = (border_y || border_x) ? 0 : c;
        }
    }
}

/* line-clear 후 보드 전체 redraw — api_blit_rect 1회로 처리.
 * deferred (win | 1) 로 호출 → 이후 piece 그리기 + refresh_field 한 번이 묶음. */
static void redraw_field(int win)
{
    int x, y;
    for (y = 0; y < FH; y++)
        for (x = 0; x < FW; x++)
            board_buf_cell(x, y, field[y][x]);
    api_blit_rect(win | 1, board_buf, BOARD_W, BOARD_H, FX0, FY0);
}

static void refresh_field(int win)
{
    api_refreshwin(win, FX0, FY0, FX0 + BOARD_W, FY0 + BOARD_H);
}

static int clear_lines(void)
{
    int y, x, n = 0, full;
    for (y = FH - 1; y >= 0; ) {
        full = 1;
        for (x = 0; x < FW; x++)
            if (field[y][x] == 0) { full = 0; break; }
        if (full) {
            int yy;
            for (yy = y; yy > 0; yy--)
                for (x = 0; x < FW; x++) field[yy][x] = field[yy - 1][x];
            for (x = 0; x < FW; x++) field[0][x] = 0;
            n++;
        } else {
            y--;
        }
    }
    return n;
}

static void lock_piece(int type, int rot, int x, int y)
{
    int i, cx, cy;
    unsigned char c = piece_color[type];
    for (i = 0; i < 4; i++) {
        cx = x + pieces[type][rot][i][0];
        cy = y + pieces[type][rot][i][1];
        if (cy >= 0 && cy < FH && cx >= 0 && cx < FW)
            field[cy][cx] = c;
    }
}

/* setdec for "0000000\0" 7자리 + null */
static void setdec(char *s, int v)
{
    int j;
    for (j = 6; j >= 0; j--) { s[j] = '0' + v % 10; v /= 10; }
    s[7] = 0;
}

static void draw_panel(int win, int score, int level, int lines, int next_type)
{
    int px = FX0 + FW * CELL + 8;
    int py = FY0;
    char s[16];

    api_boxfilwin(win + 1, px, py, px + 60, py + 200, 7);

    api_putstrwin(win + 1, px + 4, py + 4,   0, 5, "SCORE");
    setdec(s, score);
    api_putstrwin(win + 1, px + 4, py + 20,  0, 7, s);

    api_putstrwin(win + 1, px + 4, py + 40,  0, 5, "LEVEL");
    setdec(s, level);
    api_putstrwin(win + 1, px + 4, py + 56,  0, 7, s);

    api_putstrwin(win + 1, px + 4, py + 76,  0, 5, "LINES");
    setdec(s, lines);
    api_putstrwin(win + 1, px + 4, py + 92,  0, 7, s);

    api_putstrwin(win + 1, px + 4, py + 116, 0, 4, "NEXT");
    /* NEXT 셀 미리보기 (4x4, 작은 박스) */
    {
        int i, bx = px + 4, by = py + 132;
        api_boxfilwin(win + 1, bx, by, bx + 4 * 8, by + 4 * 8, 0);
        for (i = 0; i < 4; i++) {
            int dx = pieces[next_type][0][i][0];
            int dy = pieces[next_type][0][i][1];
            int x0 = bx + dx * 8;
            int y0 = by + dy * 8;
            api_boxfilwin(win + 1, x0, y0, x0 + 7, y0 + 7,
                          piece_color[next_type]);
        }
    }

    api_refreshwin(win, px, py, px + 61, py + 201);
}

/* 일정 시간 동안 키 입력을 keyflag 에 모은다. tick 단위(1tick = 1/100초). */
static void wait_ticks(int ticks, int timer, int *keyflag, int wait_enter)
{
    int j, sentinel;
    if (wait_enter) {
        sentinel = 0x0a;
    } else {
        api_settimer(timer, ticks);
        sentinel = TIMER_SIG;
    }
    for (;;) {
        j = api_getkey(1);
        if (j == sentinel) break;
        if      (j == KEY_LEFT)  keyflag[0] = 1;
        else if (j == KEY_RIGHT) keyflag[1] = 1;
        else if (j == KEY_UP)    keyflag[2] = 1;
        else if (j == KEY_DOWN)  keyflag[3] = 1;
        else if (j == ' ')       keyflag[4] = 1;
        rng_state += (unsigned)j + 17;
    }
}

void HariMain(void)
{
    int win, timer;
    char winbuf[WIN_W * WIN_H];
    int keyflag[5];
    int type, rot, px, py;
    int next_type;
    int score, lines, level;
    int fall_ticks, fall_acc;
    int i, n;

    win = api_openwin(winbuf, WIN_W, WIN_H, -1, "tetris");
    api_boxfilwin(win, 6, 27, WIN_W - 7, WIN_H - 7, 7);
    api_boxfilwin(win, FX0 - 1, FY0 - 1,
                  FX0 + FW * CELL, FY0 + FH * CELL, 0);

    timer = api_alloctimer();
    api_inittimer(timer, TIMER_SIG);

restart:
    for (i = 0; i < FH * FW; i++) ((unsigned char *)field)[i] = 0;
    score = 0;
    lines = 0;
    level = 1;
    fall_ticks = 50;        /* level 1: 0.5s */
    fall_acc = 0;
    type = rng7();
    next_type = rng7();
    rot = 0;
    px = 3;
    py = 0;

    redraw_field(win);
    draw_piece(win, type, rot, px, py, piece_color[type]);
    refresh_field(win);
    draw_panel(win, score, level, lines, next_type);

    for (;;) {
        for (i = 0; i < 5; i++) keyflag[i] = 0;
        wait_ticks(4, timer, keyflag, 0);

        /* 이전 위치 지우기 */
        draw_piece(win, type, rot, px, py, 0);

        if (keyflag[0] && !collides(type, rot, px - 1, py)) px--;
        if (keyflag[1] && !collides(type, rot, px + 1, py)) px++;
        if (keyflag[2]) {
            int nrot = (rot + 1) & 3;
            if (!collides(type, nrot, px, py)) rot = nrot;
            else if (!collides(type, nrot, px - 1, py)) { rot = nrot; px--; }
            else if (!collides(type, nrot, px + 1, py)) { rot = nrot; px++; }
        }

        fall_acc += 4;
        if (keyflag[3]) fall_acc += fall_ticks;     /* soft drop */

        if (keyflag[4]) {
            /* hard drop */
            while (!collides(type, rot, px, py + 1)) py++;
            fall_acc = fall_ticks;
        }

        if (fall_acc >= fall_ticks) {
            fall_acc = 0;
            if (!collides(type, rot, px, py + 1)) {
                py++;
            } else {
                /* lock — 잠긴 조각을 먼저 그려둔다 (프레임 시작에서 지웠으므로) */
                draw_piece(win, type, rot, px, py, piece_color[type]);
                lock_piece(type, rot, px, py);
                n = clear_lines();
                if (n > 0) {
                    static const int score_table[5] = { 0, 100, 300, 500, 800 };
                    score += score_table[n] * level;
                    lines += n;
                    level = 1 + lines / 10;
                    fall_ticks = 50 - (level - 1) * 4;
                    if (fall_ticks < 6) fall_ticks = 6;
                    redraw_field(win);
                }
                draw_panel(win, score, level, lines, next_type);
                type = next_type;
                next_type = rng7();
                rot = 0;
                px = 3;
                py = 0;
                if (collides(type, rot, px, py)) {
                    /* 그릴 위치도 없음 → game over */
                    draw_piece(win, type, rot, px, py, piece_color[type]);
                    refresh_field(win);
                    break;
                }
                draw_panel(win, score, level, lines, next_type);
            }
        }

        draw_piece(win, type, rot, px, py, piece_color[type]);
        refresh_field(win);
    }

    /* GAME OVER */
    api_boxfilwin(win, FX0 + 8, FY0 + FH * CELL / 2 - 8,
                  FX0 + FW * CELL - 9, FY0 + FH * CELL / 2 + 8, 8);
    api_putstrwin(win, FX0 + 16, FY0 + FH * CELL / 2 - 4,
                  7, 9, "GAME OVER");

    /* Enter 대기 후 재시작 */
    {
        int dummy[5] = { 0, 0, 0, 0, 0 };
        wait_ticks(0, timer, dummy, 1);
    }
    goto restart;
}
