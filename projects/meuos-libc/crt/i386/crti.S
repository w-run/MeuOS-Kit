.section .init
.globl _init
.type _init,@function
_init:
	pushl %ebp
	movl %esp, %ebp
	/* See crt/x86_64/crti.S for the link-time .init section contract. */

.section .fini
.globl _fini
.type _fini,@function
_fini:
	pushl %ebp
	movl %esp, %ebp

.section .note.GNU-stack,"",@progbits