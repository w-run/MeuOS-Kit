.arm
.section .init
.globl _init
.type _init,%function
_init:
	push {r11, lr}
	mov r11, #0
	/* See crt/x86_64/crti.S for the link-time .init section contract. */

.section .fini
.globl _fini
.type _fini,%function
_fini:
	push {r11, lr}
	mov r11, #0

.section .note.GNU-stack,"",%progbits