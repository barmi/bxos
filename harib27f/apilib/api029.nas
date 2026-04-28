[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api029.nas"]

		GLOBAL	_api_fwrite

[SECTION .text]

_api_fwrite:		; int api_fwrite(char *buf, int maxsize, int fhandle);
		PUSH	EBX
		MOV		EDX,29
		MOV		EAX,[ESP+16]		; fhandle
		MOV		ECX,[ESP+12]		; maxsize
		MOV		EBX,[ESP+8]			; buf
		INT		0x40
		POP		EBX
		RET
