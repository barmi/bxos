/* bxos.h — BxOS user-space C API.
 *
 * 모든 시스템 콜은 INT 0x40 (HRB와 동일 디스패치). EDX = function number,
 * 그 외는 함수별 인자. libbxos가 GCC inline asm으로 래핑한다.
 *
 * 기존 apilib.h 시그니처와 호환되며 (api_putchar 등 동일) 추가로
 * 좀 더 C스러운 별칭 (bxos_putchar 등) 도 함께 노출한다.
 */
#ifndef BXOS_H
#define BXOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- core / lifecycle ---------------------------------------------------- */
void  api_putchar(int c);
void  api_putstr0(const char *s);
void  api_putstr1(const char *s, int l);
void  api_end(void) __attribute__((noreturn));

/* ---- windowing ----------------------------------------------------------- */
int   api_openwin(char *buf, int xsiz, int ysiz, int col_inv, const char *title);
void  api_putstrwin(int win, int x, int y, int col, int len, const char *str);
void  api_boxfilwin(int win, int x0, int y0, int x1, int y1, int col);
void  api_point(int win, int x, int y, int col);
void  api_refreshwin(int win, int x0, int y0, int x1, int y1);
void  api_linewin(int win, int x0, int y0, int x1, int y1, int col);
void  api_closewin(int win);

/* ---- memory -------------------------------------------------------------- */
void  api_initmalloc(void);
char *api_malloc(int size);
void  api_free(char *addr, int size);

/* ---- input --------------------------------------------------------------- */
int   api_getkey(int mode);

/* ---- timer --------------------------------------------------------------- */
int   api_alloctimer(void);
void  api_inittimer(int timer, int data);
void  api_settimer(int timer, int time);
void  api_freetimer(int timer);

/* ---- audio --------------------------------------------------------------- */
void  api_beep(int tone);

/* ---- file system --------------------------------------------------------- */
int   api_fopen(const char *fname);
void  api_fclose(int fhandle);
void  api_fseek(int fhandle, int offset, int mode);
int   api_fsize(int fhandle, int mode);
int   api_fread(char *buf, int maxsize, int fhandle);
int   api_fopen_w(const char *fname);
int   api_fwrite(const char *buf, int maxsize, int fhandle);
int   api_fdelete(const char *fname);

/* ---- misc ---------------------------------------------------------------- */
int   api_cmdline(char *buf, int maxsize);
int   api_getlang(void);
int   api_getcwd(char *buf, int maxsize);

static inline int bx_getcwd(char *buf, int maxsize) {
    return api_getcwd(buf, maxsize);
}

/* ---- file management (work4 Phase 1) ------------------------------------- */
struct BX_DIRINFO {
    char         name[13];   /* "NAME.EXT" + NUL, dir = name only */
    unsigned char attr;       /* FAT attr; 0x10 = directory */
    unsigned int  size;
    unsigned int  clustno;
};
int   api_opendir(const char *path);                          /* → handle/0 */
int   api_readdir(int handle, struct BX_DIRINFO *out);        /* 1/0/-1     */
void  api_closedir(int handle);
int   api_stat(const char *path, struct BX_DIRINFO *out);     /* 0/-1       */
int   api_mkdir(const char *path);                            /* 0/<0       */
int   api_rmdir(const char *path);                            /* 0/<0       */
int   api_rename(const char *oldpath, const char *newpath);   /* 0/<0       */
int   api_exec(const char *path, int flags);                  /* 0/<0       */

/* ---- window events / resize (work4 Phase 2) ------------------------------ */
struct BX_EVENT {
    int type;
    int win;
    int x, y;
    int button;
    int key;
    int w, h;
};
#define BX_EVENT_KEY            1
#define BX_EVENT_MOUSE_DOWN     2
#define BX_EVENT_MOUSE_UP       3
#define BX_EVENT_MOUSE_MOVE     4
#define BX_EVENT_MOUSE_DBLCLK   5
#define BX_EVENT_RESIZE         6

#define BX_WIN_EV_MOUSE         0x01
#define BX_WIN_EV_RESIZE        0x02
#define BX_WIN_EV_DBLCLK        0x04

#define BX_CURSOR_ARROW         0
#define BX_CURSOR_RESIZE_NWSE   1
#define BX_CURSOR_RESIZE_WE     2
#define BX_CURSOR_RESIZE_NS     3

int   api_getevent(struct BX_EVENT *out, int mode);   /* 1=event,0=none,-1=err */
int   api_resizewin(int win, char *new_buf, int new_w, int new_h, int col_inv);
int   api_set_winevent(int win, int flags);
int   api_setcursor(int shape);

/* ---- memory layout exported by linker (linker-he2.lds) ------------------ */
extern char _he2_image_end[];   /* end of file image (= bss start)          */
extern char _he2_bss_end[];     /* end of bss        (= heap start)         */

#ifdef __cplusplus
}
#endif

#endif /* BXOS_H */
