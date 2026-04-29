void api_putchar(int c);
void api_putstr0(char *s);
void api_putstr1(char *s, int l);
void api_end(void);
int api_openwin(char *buf, int xsiz, int ysiz, int col_inv, char *title);
void api_putstrwin(int win, int x, int y, int col, int len, char *str);
void api_boxfilwin(int win, int x0, int y0, int x1, int y1, int col);
void api_initmalloc(void);
char *api_malloc(int size);
void api_free(char *addr, int size);
void api_point(int win, int x, int y, int col);
void api_refreshwin(int win, int x0, int y0, int x1, int y1);
void api_linewin(int win, int x0, int y0, int x1, int y1, int col);
void api_closewin(int win);
int api_getkey(int mode);
int api_alloctimer(void);
void api_inittimer(int timer, int data);
void api_settimer(int timer, int time);
void api_freetimer(int timer);
void api_beep(int tone);
int api_fopen(char *fname);
void api_fclose(int fhandle);
void api_fseek(int fhandle, int offset, int mode);
int api_fsize(int fhandle, int mode);
int api_fread(char *buf, int maxsize, int fhandle);
int api_fopen_w(char *fname);
int api_fwrite(char *buf, int maxsize, int fhandle);
int api_fdelete(char *fname);
int api_cmdline(char *buf, int maxsize);
int api_getlang(void);
int api_getcwd(char *buf, int maxsize);
struct BX_DIRINFO {
	char         name[13];
	unsigned char attr;
	unsigned int  size;
	unsigned int  clustno;
};
int api_opendir(const char *path);
int api_readdir(int handle, struct BX_DIRINFO *out);
void api_closedir(int handle);
int api_stat(const char *path, struct BX_DIRINFO *out);
int api_mkdir(const char *path);
int api_rmdir(const char *path);
int api_rename(const char *oldpath, const char *newpath);
int api_exec(const char *path, int flags);
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
int api_getevent(struct BX_EVENT *out, int mode);
int api_resizewin(int win, char *new_buf, int new_w, int new_h, int col_inv);
int api_set_winevent(int win, int flags);
