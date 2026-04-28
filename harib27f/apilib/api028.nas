[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api028.nas"]

		GLOBAL	_api_fopen_w

[SECTION .text]

_api_fopen_w:		; int api_fopen_w(char *fname);
		PUSH	EBX
		MOV		EDX,28
		MOV		EBX,[ESP+8]			; fname
		INT		0x40
		POP		EBX
		RET
