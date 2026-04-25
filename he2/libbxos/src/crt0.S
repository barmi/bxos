/* crt0.S — HE2 header + program entry trampoline.
 *
 * 링커 스크립트 linker-he2.lds 가 .he2_header 섹션을 파일 offset 0 에
 * 배치하고, _he2_image_end / _he2_bss_size 같은 링커 심볼을 정의한다.
 * 따라서 헤더 안의 entry_off, image_size, bss_size 는 GNU ld 가 자동
 * 으로 채워준다 — 별도의 패치 도구 불필요.
 *
 * stack_size / heap_size 는 .weak 으로 잡힌 디폴트 값을 쓰지만, 앱이
 *   -Wl,--defsym=_bxos_stack_size=32768
 * 처럼 링크 시 덮어쓸 수 있다.
 */

    /* ─── 디폴트 stack/heap 크기 (weak — 앱이 덮어쓸 수 있음) ─── */
    .weak _bxos_stack_size
    .set  _bxos_stack_size, 16384
    .weak _bxos_heap_size
    .set  _bxos_heap_size, 1048576       /* 1MB */
    .weak _bxos_app_flags
    .set  _bxos_app_flags, 0             /* console subsystem */

    /* ─── HE2 헤더 (32B) ───────────────────────────────────────── */
    .section .he2_header, "a", @progbits
    .globl  _bxos_he2_header
_bxos_he2_header:
    .ascii  "HE2"
    .byte   0
    .short  1                       /* version                     */
    .short  32                      /* header_size                 */
    .long   _start                  /* entry_off                   */
    .long   _he2_image_end          /* image_size                  */
    .long   _he2_bss_size           /* bss_size                    */
    .long   _bxos_stack_size        /* stack_size                  */
    .long   _bxos_heap_size         /* heap_size                   */
    .long   _bxos_app_flags         /* flags                       */

    /* ─── 진입점 _start ────────────────────────────────────────── */
    .section .text._start, "ax", @progbits
    .globl  _start
    .type   _start, @function
_start:
    /* HariMain() 은 인자 없음, 반환값 없음. 끝나면 api_end() 호출.   */
    call    HariMain
    /* api_end()는 noreturn 이지만 안전하게 INT 0x40 로 직접 호출.   */
    movl    $4, %edx
    int     $0x40
1:  hlt
    jmp     1b
    .size   _start, . - _start
