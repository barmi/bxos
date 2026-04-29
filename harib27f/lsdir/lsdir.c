/* lsdir.c — work4 Phase 1 verification.
 *
 * 사용자 디렉터리 API (api_opendir/readdir/closedir/stat) 가 정상 동작하는지
 * 콘솔에 한 디렉터리 entries 를 한 줄씩 찍어 본다.                            */
#include "apilib.h"

static void print_uint(unsigned int v)
{
    char buf[12];
    int i = 0;
    if (v == 0) {
        api_putchar('0');
        return;
    }
    while (v > 0 && i < (int) sizeof buf) {
        buf[i++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (--i >= 0) {
        api_putchar(buf[i]);
    }
}

void HariMain(void)
{
    char cmdline[128];
    char path[128];
    char *p;
    int h, n, i;
    struct BX_DIRINFO ent;

    api_cmdline(cmdline, sizeof cmdline);
    /* skip program name */
    for (p = cmdline; *p > ' '; p++) { }
    while (*p == ' ') p++;
    if (*p == 0) {
        path[0] = '/';
        path[1] = 0;
    } else {
        for (i = 0; i < (int) sizeof path - 1 && p[i] != 0 && p[i] != ' '; i++) {
            path[i] = p[i];
        }
        path[i] = 0;
    }

    h = api_opendir(path);
    if (h == 0) {
        api_putstr0("opendir failed: ");
        api_putstr0(path);
        api_putchar('\n');
        api_end();
    }
    api_putstr0("listing ");
    api_putstr0(path);
    api_putchar('\n');
    n = 0;
    for (;;) {
        i = api_readdir(h, &ent);
        if (i < 0) {
            api_putstr0("readdir error\n");
            break;
        }
        if (i == 0) break;
        api_putstr0(ent.name);
        if ((ent.attr & 0x10) != 0) {
            api_putstr0("  <DIR>");
        } else {
            api_putstr0("  ");
            print_uint(ent.size);
        }
        api_putchar('\n');
        n++;
    }
    api_closedir(h);
    api_putstr0("entries: ");
    print_uint((unsigned int) n);
    api_putchar('\n');
    api_end();
}
