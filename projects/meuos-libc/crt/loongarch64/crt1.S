.text

/* Linux loongarch64 process entry.  The kernel places argc at [sp], followed
 * by argv[], envp[], then auxv[].  LoongArch64 LP64D requires sp to be 16-byte
 * aligned at function-call boundaries.  We set up TLS ($tp) before calling
 * main so that _Thread_local variables (notably errno) work, then terminate
 * via exit() so that atexit handlers run and stdio buffers are flushed (important
 * when stdout is redirected to a file rather than a line-buffered tty).
 *
 * Unlike aarch64 (which needs `msr tpidr_el0` to program the thread pointer),
 * loongarch64's $tp (r21) is a normal user-writable GPR: the thread pointer is
 * written directly with `move $tp, $a0` after __meuos_tls_init returns the TLS
 * base.  The kernel programs $tp for new threads via CLONE_SETTLS. */

.globl _start
.type _start,@function
_start:
	move $fp, $zero			/* clear frame pointer */
	move $s0, $sp			/* save kernel sp (argc/argv/envp/auxv) */
	bstrins.d $sp, $zero, 3, 0	/* align sp for the calls below */

	/* argc = [s0]; argv = s0+8; envp = argv + (argc+1)*8 */
	ld.d $a0, $s0, 0			/* argc */
	addi.d $a1, $s0, 8			/* argv */
	slli.d $a2, $a0, 3			/* argc * 8 */
	add.d $a2, $a2, $a1			/* &argv[argc] */
	addi.d $a2, $a2, 8			/* envp */

	/* store envp into the global environ */
	la $a3, environ
	st.d $a2, $a3, 0

	/* tp = __meuos_tls_init(envp); it returns the TLS base pointer */
	move $a0, $a2
	bl __meuos_tls_init
	move $tp, $a0

	/* reload argc/argv/envp from the saved kernel sp */
	ld.d $a0, $s0, 0
	addi.d $a1, $s0, 8
	slli.d $a2, $a0, 3
	add.d $a2, $a2, $a1
	addi.d $a2, $a2, 8

	bl main

	/* exit() runs atexit handlers and fflush(NULL) before the raw _exit
	 * syscall, so buffered printf output is not lost. */
	b exit

.bss
.align 3
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0

.section .note.GNU-stack,"",@progbits
