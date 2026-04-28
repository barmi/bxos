[FORMAT "WCOFF"]
[INSTRSET "i486p"]
[BITS 32]
[FILE "api031.nas"]

		GLOBAL	_api_getcwd

[SECTION .text]

_api_getcwd:		; int api_getcwd(char *buf, int maxsize);
		PUSH	EBX
		MOV		EDX,31
		MOV		ECX,[ESP+12]		; maxsize
		MOV		EBX,[ESP+8]			; buf
		INT		0x40
		POP		EBX
		RET
