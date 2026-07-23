.text

/* Linux riscv64 process entry. The kernel places argc at [sp], followed by
 * argv[], envp[], then auxv[]. RISC-V LP64D requires sp to be 16-byte aligned
 * at function-call boundaries. We set up TLS (tp) before calling main so that
 * _Thread_local variables (notably errno) work, then terminate via exit()
 * so that atexit handlers run and stdio buffers are flushed (important when
 * stdout is redirected to a file rather than a line-buffered tty).
 *
 * Unlike aarch64 (which needs `msr tpidr_el0` to program the thread pointer),
 * riscv64 has no arch_prctl-like syscall: the thread pointer lives in the tp
 * register (x4) and is written directly with `mv tp, a0`.
 * __meuos_tls_init returns the TLS base pointer, which we install here. The
 * kernel programs tp the same way for new threads via CLONE_SETTLS. */

.globl _start
.type _start,@function
_start:
	mv s0, sp			/* save kernel sp (argc/argv/envp/auxv) */
	andi sp, sp, -16		/* align sp for the calls below */

	/* argc = [s0]; argv = s0+8; envp = argv + (argc+1)*8 */
	ld a0, 0(s0)			/* argc */
	addi a1, s0, 8			/* argv */
	slli a2, a0, 3			/* argc * 8 */
	add a2, a2, a1			/* &argv[argc] */
	addi a2, a2, 8			/* envp */

	/* store envp into the global environ */
	lla a3, environ
	sd a2, 0(a3)

	/* tp = __meuos_tls_init(envp); it returns the TLS base pointer */
	mv a0, a2
	call __meuos_tls_init
	mv tp, a0

	/* reload argc/argv/envp from the saved kernel sp */
	ld a0, 0(s0)			/* argc */
	addi a1, s0, 8			/* argv */
	slli a2, a0, 3
	add a2, a2, a1
	addi a2, a2, 8			/* envp */

	call main

	/* exit() runs atexit handlers and fflush(NULL) before the raw _exit
	 * syscall, so buffered printf output is not lost. */
	tail exit

.bss
.align 3
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0

.section .note.GNU-stack,"",@progbits
