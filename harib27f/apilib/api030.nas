[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api030.nas"]

		GLOBAL	_api_fdelete

[SECTION .text]

_api_fdelete:		; int api_fdelete(char *fname);
		PUSH	EBX
		MOV		EDX,30
		MOV		EBX,[ESP+8]			; fname
		INT		0x40
		POP		EBX
		RET
