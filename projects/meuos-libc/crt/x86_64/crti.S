.section .init
.globl _init
.type _init,@function
_init:
	pushq %rbp
	movq %rsp, %rbp
	/* The linker's .init section is collected into a single function-like
	 * blob ending in crtn.S's epilogue; this prologue matches its `popq
	 * %rbp; ret` so the body is balanced. */

.section .fini
.globl _fini
.type _fini,@function
_fini:
	pushq %rbp
	movq %rsp, %rbp
	/* Mirrors the epilogue in crtn.S. */

.section .note.GNU-stack,"",@progbits