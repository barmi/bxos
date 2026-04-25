/* linker-he2.lds — Linker script for BxOS HE2 user executables.
 *
 * 메모리 모델:
 *   파일 = HE2 헤더 (32B) + .text + .rodata + .data
 *   런타임에 커널이 그 뒤로 .bss + heap + stack 영역을 0으로 잡아준다.
 *
 * 모든 섹션은 VMA 0 부터 링크. CS / DS 는 같은 base 를 가지므로 데이터
 * 참조 오프셋과 코드 오프셋이 그대로 일치한다.
 *
 * 출력은 ELF (i386). 이후 objcopy -O binary 로 평면 이미지를 뽑고,
 * 그 결과가 곧 .he2 파일이다 — 헤더가 이미 ELF 안에 박혀 있으므로
 * 별도 패치가 필요 없다.
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x0;

    /* HE2 헤더는 반드시 파일 첫 32B. */
    .he2_header : ALIGN(1) {
        KEEP(*(.he2_header))
    }

    /* 코드 — _start 가 헤더 직후에 오도록 배치 */
    .text : ALIGN(4) {
        KEEP(*(.text._start))
        *(.text .text.*)
    }

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4) {
        *(.data .data.*)
    }

    . = ALIGN(16);
    _he2_image_end = .;          /* 파일 image 의 끝                 */

    .bss (NOLOAD) : ALIGN(16) {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    _he2_bss_end = .;            /* bss 의 끝 = heap 시작            */
    _he2_bss_size = _he2_bss_end - _he2_image_end;
    _bxos_heap_start = _he2_bss_end;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu.version*)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.hash)
        *(.gnu.hash)
        *(.interp)
    }
}
