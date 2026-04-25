/* he2.h — HE2 binary header layout shared between userspace and kernel. */
#ifndef BXOS_HE2_H
#define BXOS_HE2_H

#include <stdint.h>

#define HE2_MAGIC       "HE2"           /* 4 bytes incl. trailing NUL */
#define HE2_MAGIC_LE32  0x00324548U     /* 'H'|'E'<<8|'2'<<16|0<<24    */
#define HE2_VERSION     1
#define HE2_HEADER_SIZE 32
#define HE2_FLAG_SUBSYSTEM_MASK 0x00000003u
#define HE2_SUBSYSTEM_CONSOLE   0x00000000u
#define HE2_SUBSYSTEM_WINDOW    0x00000001u

struct he2_header {
    char     magic[4];      /* "HE2\0"                              */
    uint16_t version;       /* HE2_VERSION                          */
    uint16_t header_size;   /* HE2_HEADER_SIZE                      */
    uint32_t entry_off;     /* entry point offset (DS == file == 0) */
    uint32_t image_size;    /* file image size                      */
    uint32_t bss_size;      /* zeroed bytes after image             */
    uint32_t stack_size;    /* requested stack                      */
    uint32_t heap_size;     /* requested heap (api_initmalloc area) */
    uint32_t flags;         /* subsystem flags                      */
};

#endif /* BXOS_HE2_H */
