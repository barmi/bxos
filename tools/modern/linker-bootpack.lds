/*
 * tools/modern/linker-bootpack.lds
 *
 * 커널 bootpack.hrb 용 링커 스크립트.
 *
 * 출력 레이아웃 (objcopy -O binary 후 flat 바이너리):
 *   0x00 - 0x1F : startup_kernel.o 의 'TIMES 32 db 0' (HRB 헤더 자리, hrbify.py 가 덮음)
 *   0x20 +      : startup_kernel 의 _HariStartup
 *               : 그 외 .text, .rodata, .data, .bss
 *
 * `i686-elf-ld -m elf_i386 -T linker-bootpack.lds -o bootpack.elf <objects...>` 로 호출.
 * 그 후 `objcopy -O binary bootpack.elf bootpack.bin`.
 * 그 다음 `hrbify.py --in bootpack.bin --out bootpack.hrb --stack-top 3136k --esp-init 3136k`.
 *
 * 주의: startup_kernel 의 .text.startup 이 가장 먼저 배치되도록 명시적으로 둔다.
 *       그 안의 32B zero-fill 이 곧 HRB 헤더 자리가 된다.
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_HariStartup)   /* nask/cc1 컨벤션 유지: 모든 심볼에 leading underscore */

SECTIONS
{
    . = 0x0;

    .text : {
        *(.text.startup)        /* startup_kernel.o : 32B 헤더 자리 + HariStartup */
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4) {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
