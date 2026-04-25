; tools/modern/startup_kernel.s
;
; 커널 진입점 + HRB 32B 헤더 자리.
;
; asmhead.bin 이 'JMP DWORD 2*8:0x1b' 로 점프하므로, 출력 .bin 의 첫 0x20 바이트는
; HRB 헤더(0x1B 위치에 jmp opcode), 그리고 0x20 위치에 _HariStartup 가 와야 한다.
;
; 가장 깔끔한 방법은 startup 자체가 처음에 'TIMES 32 db 0' 으로 32바이트를 비워두고,
; 링커가 이 startup 의 .text.startup 을 가장 먼저 배치하는 것이다.
; hrbify.py 가 이후에 그 0 으로 채워둔 첫 32바이트를 진짜 HRB 헤더로 덮어쓴다.
;
; 어셈블: nasm -f elf32 -o startup_kernel.o startup_kernel.s

BITS 32
CPU 486

; nask 와 옛 cc1 은 _ 접두 심볼을 강제하므로 우리도 그 convention 유지.
; (Makefile.modern 의 -fleading-underscore 와 짝)
GLOBAL _HariStartup
EXTERN _HariMain

SECTION .text.startup

; ─── 0x00 - 0x1F : HRB 헤더 자리 (zero-fill, hrbify.py 가 채움) ──────
TIMES 32 db 0

; ─── 0x20 : 진입점 ──────────────────────────────────────────────────
_HariStartup:
    push    ebp
    mov     ebp, esp
    pop     ebp
    jmp     _HariMain
