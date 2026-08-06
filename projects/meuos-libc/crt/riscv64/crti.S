.section .init
.globl _init
.type _init,@function
_init:
	addi sp, sp, -16
	sd ra, 8(sp)
	sd s0, 0(sp)
	addi s0, sp, 16
	/* See crt/x86_64/crti.S for the link-time .init section contract. */

.section .fini
.globl _fini
.type _fini,@function
_fini:
	addi sp, sp, -16
	sd ra, 8(sp)
	sd s0, 0(sp)
	addi s0, sp, 16

.section .note.GNU-stack,"",@progbits