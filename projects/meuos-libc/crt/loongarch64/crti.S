.section .init
.globl _init
.type _init,@function
_init:
	addi.d $sp, $sp, -16
	st.d $ra, $sp, 8
	st.d $fp, $sp, 0
	addi.d $fp, $sp, 16
	/* See crt/x86_64/crti.S for the link-time .init section contract. */

.section .fini
.globl _fini
.type _fini,@function
_fini:
	addi.d $sp, $sp, -16
	st.d $ra, $sp, 8
	st.d $fp, $sp, 0
	addi.d $fp, $sp, 16

.section .note.GNU-stack,"",@progbits