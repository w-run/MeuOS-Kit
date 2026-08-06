.section .init
.globl _init
.type _init,@function
_init:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	/* See crt/x86_64/crti.S for the link-time .init section contract. */

.section .fini
.globl _fini
.type _fini,@function
_fini:
	stp x29, x30, [sp, #-16]!
	mov x29, sp

.section .note.GNU-stack,"",@progbits