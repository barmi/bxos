/* syscall.c — All BxOS user-space syscalls implemented as GCC inline asm.
 *
 * 이전엔 apilib/api001.nas .. api027.nas 27개 파일로 분산되어 있었지만,
 * 이제는 한 파일에서 inline asm 으로 한 줄씩 정의한다. 빌드/링크/디버그가
 * 훨씬 단순해진다.
 *
 * 호출 규약 (HRB 와 동일):
 *   EDX = function number
 *   EBX, ECX, ESI, EDI, EBP, EAX = function-specific args
 *   return value = EAX
 *
 * 커널 측 _asm_hrb_api 는 PUSHAD/POPAD로 호출자의 모든 GPR을 복원하므로,
 * inline asm clobber 는 (반환값을 받는 EAX 외에는) 적어두지 않아도 된다.
 *
 * NB: PIC 빌드는 EBX 를 GOT 로 잡으므로 "b" constraint 가 위험하다.
 *     CMake 쪽에서 -fno-pic / -fno-pie 를 강제한다.
 */

#include "bxos.h"
#include "he2.h"

/* HE2 헤더는 .he2_header 섹션의 첫 32B = DS offset 0 에 위치한다.
 * 런타임에 heap_size 등을 읽기 위해 extern 으로 잡아둔다.            */
extern const struct he2_header _bxos_he2_header;

/* ─── 공용 매크로 ──────────────────────────────────────────────────── */

/* 0-arg, no return */
#define SYS0(num)                                                    \
    __asm__ volatile ("int $0x40" :: "d"(num) : "memory")

/* arg in EAX, no return */
#define SYS_A(num, a)                                                \
    __asm__ volatile ("int $0x40" :: "d"(num), "a"(a) : "memory")

/* arg in EBX */
#define SYS_B(num, b)                                                \
    __asm__ volatile ("int $0x40" :: "d"(num), "b"(b) : "memory")

/* args in EBX, ECX */
#define SYS_BC(num, b, c)                                            \
    __asm__ volatile ("int $0x40" :: "d"(num), "b"(b), "c"(c)        \
                      : "memory")

/* return EAX, args in EBX, ECX */
#define SYS_R_BC(num, b, c)                                          \
    ({  int _r;                                                       \
        __asm__ volatile ("int $0x40"                                \
                          : "=a"(_r)                                 \
                          : "d"(num), "b"(b), "c"(c)                 \
                          : "memory");                                \
        _r;                                                          \
    })

/* return EAX, arg in EAX */
#define SYS_R_A(num, a)                                              \
    ({  int _r;                                                       \
        __asm__ volatile ("int $0x40"                                \
                          : "=a"(_r)                                 \
                          : "d"(num), "0"(a)                         \
                          : "memory");                                \
        _r;                                                          \
    })

/* ─── 1: putchar ──────────────────────────────────────────────────── */
void api_putchar(int c) {
    SYS_A(1, c);
}

/* ─── 2: putstr0 ──────────────────────────────────────────────────── */
void api_putstr0(const char *s) {
    SYS_B(2, s);
}

/* ─── 3: putstr1 ──────────────────────────────────────────────────── */
void api_putstr1(const char *s, int l) {
    SYS_BC(3, s, l);
}

/* ─── 4: end ──────────────────────────────────────────────────────── */
void api_end(void) {
    SYS0(4);
    __builtin_unreachable();
}

/* ─── 5: openwin
 *   EBX=buf, ESI=xsiz, EDI=ysiz, EAX=col_inv, ECX=title
 *   return EAX = window handle                                       */
int api_openwin(char *buf, int xsiz, int ysiz, int col_inv, const char *title)
{
    int win;
    __asm__ volatile ("int $0x40"
        : "=a"(win)
        : "d"(5), "b"(buf), "S"(xsiz), "D"(ysiz), "0"(col_inv), "c"(title)
        : "memory");
    return win;
}

/* ─── 6: putstrwin
 *   EBX=win, ESI=x, EDI=y, EAX=col, ECX=len, EBP=str                 */
void api_putstrwin(int win, int x, int y, int col, int len, const char *str)
{
    /* EBP 는 GCC가 frame pointer 로 잡고 있을 수 있어 직접 constraint 로
     * 사용할 수 없다. push/pop 으로 수동 보존하면서 사용한다.        */
    __asm__ volatile (
        "pushl %%ebp\n\t"
        "movl  %6, %%ebp\n\t"
        "int   $0x40\n\t"
        "popl  %%ebp"
        :
        : "d"(6), "b"(win), "S"(x), "D"(y), "a"(col), "c"(len), "rm"(str)
        : "memory");
}

/* ─── 7: boxfilwin
 *   EBX=win, EAX=x0, ECX=y0, ESI=x1, EDI=y1, EBP=col                 */
void api_boxfilwin(int win, int x0, int y0, int x1, int y1, int col)
{
    __asm__ volatile (
        "pushl %%ebp\n\t"
        "movl  %6, %%ebp\n\t"
        "int   $0x40\n\t"
        "popl  %%ebp"
        :
        : "d"(7), "b"(win), "a"(x0), "c"(y0), "S"(x1), "D"(y1), "rm"(col)
        : "memory");
}

/* ─── 8/9/10: malloc 패밀리
 *
 *   HE2 에선 heap 영역이 [_he2_bss_end ..  _he2_bss_end+heap_size).
 *   memman bookkeeping 용으로 32K 를 떼고 나머지를 free 영역으로 잡는다.
 */

#define BXOS_MEMMAN_RESERVE  (32 * 1024)

void api_initmalloc(void) {
    char *area      = _he2_bss_end;
    char *use_start = area + BXOS_MEMMAN_RESERVE;
    int   heap_sz   = (int) _bxos_he2_header.heap_size;
    int   use_size  = heap_sz - BXOS_MEMMAN_RESERVE;
    if (use_size < 0) use_size = 0;

    /* edx=8: memman_init(EBX+ds), memman_free(EBX+ds, EAX, ECX) */
    __asm__ volatile ("int $0x40"
        :
        : "d"(8), "b"(area), "a"(use_start), "c"(use_size)
        : "memory");
}

char *api_malloc(int size) {
    char *p;
    /* edx=9: memman_alloc(EBX+ds, ECX) → EAX */
    __asm__ volatile ("int $0x40"
        : "=a"(p)
        : "d"(9), "b"(_he2_bss_end), "c"(size)
        : "memory");
    return p;
}

void api_free(char *addr, int size) {
    /* edx=10: memman_free(EBX+ds, EAX, ECX) */
    __asm__ volatile ("int $0x40"
        :
        : "d"(10), "b"(_he2_bss_end), "a"(addr), "c"(size)
        : "memory");
}

/* ─── 11: point — EBX=win, ESI=x, EDI=y, EAX=col ─────────────────── */
void api_point(int win, int x, int y, int col) {
    __asm__ volatile ("int $0x40"
        :
        : "d"(11), "b"(win), "S"(x), "D"(y), "a"(col)
        : "memory");
}

/* ─── 12: refreshwin — EBX=win, EAX=x0, ECX=y0, ESI=x1, EDI=y1 ──── */
void api_refreshwin(int win, int x0, int y0, int x1, int y1) {
    __asm__ volatile ("int $0x40"
        :
        : "d"(12), "b"(win), "a"(x0), "c"(y0), "S"(x1), "D"(y1)
        : "memory");
}

/* ─── 13: linewin — EBX=win, EAX=x0, ECX=y0, ESI=x1, EDI=y1, EBP=col */
void api_linewin(int win, int x0, int y0, int x1, int y1, int col) {
    __asm__ volatile (
        "pushl %%ebp\n\t"
        "movl  %6, %%ebp\n\t"
        "int   $0x40\n\t"
        "popl  %%ebp"
        :
        : "d"(13), "b"(win), "a"(x0), "c"(y0), "S"(x1), "D"(y1), "rm"(col)
        : "memory");
}

/* ─── 14: closewin — EBX=win ──────────────────────────────────────── */
void api_closewin(int win) {
    SYS_B(14, win);
}

/* ─── 15: getkey — EAX=mode → EAX ────────────────────────────────── */
int api_getkey(int mode) {
    return SYS_R_A(15, mode);
}

/* ─── 16: alloctimer (no args) → EAX ─────────────────────────────── */
int api_alloctimer(void) {
    int t;
    __asm__ volatile ("int $0x40"
        : "=a"(t) : "d"(16) : "memory");
    return t;
}

/* ─── 17: inittimer — EBX=timer, EAX=data ────────────────────────── */
void api_inittimer(int timer, int data) {
    __asm__ volatile ("int $0x40"
        : : "d"(17), "b"(timer), "a"(data) : "memory");
}

/* ─── 18: settimer — EBX=timer, EAX=time ─────────────────────────── */
void api_settimer(int timer, int time) {
    __asm__ volatile ("int $0x40"
        : : "d"(18), "b"(timer), "a"(time) : "memory");
}

/* ─── 19: freetimer — EBX=timer ──────────────────────────────────── */
void api_freetimer(int timer) {
    SYS_B(19, timer);
}

/* ─── 20: beep — EAX=tone ────────────────────────────────────────── */
void api_beep(int tone) {
    SYS_A(20, tone);
}

/* ─── 21: fopen — EBX=fname → EAX = handle ───────────────────────── */
int api_fopen(const char *fname) {
    int h;
    __asm__ volatile ("int $0x40"
        : "=a"(h) : "d"(21), "b"(fname) : "memory");
    return h;
}

/* ─── 22: fclose — EAX=handle ─────────────────────────────────────── */
void api_fclose(int fhandle) {
    SYS_A(22, fhandle);
}

/* ─── 23: fseek — EAX=handle, EBX=offset, ECX=mode ─────────────────
 *
 * 주의: console.c 의 ELSE-IF 분기에서 fh = (FILEHANDLE*)eax 인 것을
 *       확인했음. 따라서 EAX 가 handle, EBX 가 offset 이다.
 */
void api_fseek(int fhandle, int offset, int mode) {
    __asm__ volatile ("int $0x40"
        : : "d"(23), "a"(fhandle), "b"(offset), "c"(mode) : "memory");
}

/* ─── 24: fsize — EAX=handle, ECX=mode → EAX ──────────────────────── */
int api_fsize(int fhandle, int mode) {
    int r;
    __asm__ volatile ("int $0x40"
        : "=a"(r) : "d"(24), "0"(fhandle), "c"(mode) : "memory");
    return r;
}

/* ─── 25: fread — EBX=buf, ECX=maxsize, EAX=fhandle → EAX = read    */
int api_fread(char *buf, int maxsize, int fhandle) {
    int r;
    __asm__ volatile ("int $0x40"
        : "=a"(r) : "d"(25), "b"(buf), "c"(maxsize), "0"(fhandle) : "memory");
    return r;
}

/* ─── 26: cmdline — EBX=buf, ECX=maxsize → EAX = len ──────────────── */
int api_cmdline(char *buf, int maxsize) {
    int r;
    __asm__ volatile ("int $0x40"
        : "=a"(r) : "d"(26), "b"(buf), "c"(maxsize) : "memory");
    return r;
}

/* ─── 27: getlang → EAX ───────────────────────────────────────────── */
int api_getlang(void) {
    int r;
    __asm__ volatile ("int $0x40"
        : "=a"(r) : "d"(27) : "memory");
    return r;
}

/* ─── 28: fopen_w — EBX=fname → EAX = write handle ───────────────── */
int api_fopen_w(const char *fname) {
    int h;
    __asm__ volatile ("int $0x40"
            : "=a"(h) : "d"(28), "b"(fname) : "memory");
    return h;
}

/* ─── 29: fwrite — EBX=buf, ECX=maxsize, EAX=fhandle → EAX = written */
int api_fwrite(const char *buf, int maxsize, int fhandle) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(29), "b"(buf), "c"(maxsize), "0"(fhandle) : "memory");
    return r;
}

/* ─── 30: fdelete — EBX=fname → EAX = 0 on success, -1 on failure ─── */
int api_fdelete(const char *fname) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(30), "b"(fname) : "memory");
    return r;
}

/* ─── 31: getcwd — EBX=buf, ECX=maxsize → EAX = len, -1 on failure ── */
int api_getcwd(char *buf, int maxsize) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(31), "b"(buf), "c"(maxsize) : "memory");
    return r;
}

/* ─── 32: opendir — EBX=path → EAX = handle (0=fail) ─────────────── */
int api_opendir(const char *path) {
    int h;
    __asm__ volatile ("int $0x40"
            : "=a"(h) : "d"(32), "b"(path) : "memory");
    return h;
}

/* ─── 33: readdir — EBX=handle, ECX=out → EAX = 1/0/-1 ───────────── */
int api_readdir(int handle, struct BX_DIRINFO *out) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(33), "b"(handle), "c"(out) : "memory");
    return r;
}

/* ─── 34: closedir — EBX=handle ──────────────────────────────────── */
void api_closedir(int handle) {
    SYS_B(34, handle);
}

/* ─── 35: stat — EBX=path, ECX=out → EAX = 0/-1 ──────────────────── */
int api_stat(const char *path, struct BX_DIRINFO *out) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(35), "b"(path), "c"(out) : "memory");
    return r;
}

/* ─── 36: mkdir — EBX=path → EAX = 0/<0 ──────────────────────────── */
int api_mkdir(const char *path) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(36), "b"(path) : "memory");
    return r;
}

/* ─── 37: rmdir — EBX=path → EAX = 0/<0 ──────────────────────────── */
int api_rmdir(const char *path) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(37), "b"(path) : "memory");
    return r;
}

/* ─── 38: rename — EBX=oldpath, ECX=newpath → EAX = 0/<0 ─────────── */
int api_rename(const char *oldpath, const char *newpath) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(38), "b"(oldpath), "c"(newpath) : "memory");
    return r;
}

/* ─── 39: exec — EBX=path, ECX=flags → EAX = 0/<0 ────────────────── */
int api_exec(const char *path, int flags) {
    int r;
    __asm__ volatile ("int $0x40"
            : "=a"(r) : "d"(39), "b"(path), "c"(flags) : "memory");
    return r;
}
