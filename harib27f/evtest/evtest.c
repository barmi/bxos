/* evtest.c — work4 Phase 2 검증.
 *
 * 작은 창을 열고 client area 의 마우스 이벤트와 resize 이벤트를 시각화한다.
 *   - MOUSE_DOWN / UP / MOVE → client 위치에 점/십자 표시
 *   - RESIZE → 새 buffer 를 할당하고 api_resizewin 으로 교체, 프레임 다시 그림
 *   - KEY q → 종료
 *
 * window subsystem 앱이라서 console 입력을 받지 않고 api_getevent(mode=1) 로
 * 모든 이벤트를 단일 stream 으로 처리한다. api_getkey 와 mix 하지 않는다.    */
#include "apilib.h"

#define INIT_W   200
#define INIT_H   140
#define BG       7    /* 흰색  */
#define FG       0    /* 검정  */
#define DOT      4    /* 파랑  */

static int g_w, g_h;

static void draw_frame_text(int win)
{
    api_boxfilwin(win, 0, 0, g_w - 1, g_h - 1, BG);
    api_putstrwin(win, 6, 6, FG, 14, "evtest: q=quit");
}

void HariMain(void)
{
    char *buf, *new_buf;
    int win;
    struct BX_EVENT ev;
    int last_x = -1, last_y = -1;

    api_initmalloc();
    g_w = INIT_W;
    g_h = INIT_H;
    buf = api_malloc(g_w * g_h);
    win = api_openwin(buf, g_w, g_h, -1, "evtest");
    api_set_winevent(win, BX_WIN_EV_MOUSE | BX_WIN_EV_RESIZE);
    draw_frame_text(win);
    api_refreshwin(win, 0, 0, g_w, g_h);

    for (;;) {
        if (api_getevent(&ev, 1) != 1) {
            continue;
        }
        if (ev.type == BX_EVENT_KEY) {
            if (ev.key == 'q' || ev.key == 0x1b) {
                break;
            }
        } else if (ev.type == BX_EVENT_MOUSE_DOWN) {
            int cx = ev.x + 3;
            int cy = ev.y + 21;
            api_boxfilwin(win, cx - 2, cy - 2, cx + 2, cy + 2, DOT);
            last_x = ev.x;
            last_y = ev.y;
        } else if (ev.type == BX_EVENT_MOUSE_MOVE) {
            int cx = ev.x + 3;
            int cy = ev.y + 21;
            if (ev.button & 1) {
                api_point(win, cx, cy, DOT);
            }
        } else if (ev.type == BX_EVENT_RESIZE) {
            int nw = ev.w, nh = ev.h;
            if (nw < 80) nw = 80;
            if (nh < 60) nh = 60;
            new_buf = api_malloc(nw * nh);
            if (new_buf != 0) {
                int old_w = g_w, old_h = g_h;
                g_w = nw;
                g_h = nh;
                api_resizewin(win, new_buf, g_w, g_h, -1);
                draw_frame_text(win);
                api_refreshwin(win, 0, 0, g_w, g_h);
                /* 이전 buffer 는 호출자(앱)가 free. */
                api_free(buf, old_w * old_h);
                buf = new_buf;
            }
        }
        (void) last_x; (void) last_y;
    }
    api_closewin(win);
    api_end();
}
