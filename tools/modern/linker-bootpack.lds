/*
 * tools/modern/linker-bootpack.lds
 *
 * 커널 bootpack.hrb 용 링커 스크립트.
 *
 * 출력 레이아웃 (objcopy -O binary 후 flat 바이너리):
 *   0x00 - 0x1F : startup_kernel.o 의 'TIMES 32 db 0' (HRB 헤더 자리, hrbify.py 가 덮음)
 *   0x20 +      : startup_kernel 의 _HariStartup 및 .text
 *   text 뒤     : .rodata/.data/.bss 의 초기 이미지
 *
 * 실행 시 asmhead.nas 는 HRB 헤더의 data_file/data_size 를 보고 초기 이미지를
 * esp_init(0x310000) 주소로 복사한다. 그래서 코드(.text)는 CS base=BOTPAK 의
 * 낮은 offset 에 두고, C 코드가 DS=flat 으로 참조하는 상수/전역 데이터는
 * VMA 0x310000 에 둔다.
 *
 * `i686-elf-ld -m elf_i386 -T linker-bootpack.lds -o bootpack.elf <objects...>` 로 호출.
 * 그 후 `objcopy -O binary bootpack.elf bootpack.bin`.
 * 그 다음 `hrbify.py` 가 linker symbol 에서 data_file/data_size 를 받아 HRB 헤더를 채운다.
 *
 * 주의: startup_kernel 의 .text.startup 이 가장 먼저 배치되도록 명시적으로 둔다.
 *       그 안의 32B zero-fill 이 곧 HRB 헤더 자리가 된다.
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_HariStartup)   /* nask/cc1 컨벤션 유지: 모든 심볼에 leading underscore */

DATA_VMA = 0x00310000;

SECTIONS
{
    . = 0x0;

    .text : {
        *(.text.startup)        /* startup_kernel.o : 32B 헤더 자리 + HariStartup */
        *(.text)
        *(.text.*)
    }

    __hrb_data_lma = ALIGN(., 4);

    . = DATA_VMA;
    .data ALIGN(4) : AT(__hrb_data_lma) {
        __hrb_data_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
        __hrb_data_end = .;
    }

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
