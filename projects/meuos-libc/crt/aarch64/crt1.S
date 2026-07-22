.text

/* Linux aarch64 process entry.  The kernel supplies argc at [sp] followed by
 * argv[] and envp[].  AAPCS64 requires SP to be 16-byte aligned at function
 * call boundaries.  Set up TLS (TPIDR_EL0) before calling main so that
 * _Thread_local variables (notably errno) work, then terminate via exit()
 * so that atexit handlers run and stdio buffers are flushed (important
 * when stdout is redirected to a file rather than a line-buffered tty). */
.globl _start
.type _start,@function
_start:
	mov x29, #0		/* clear frame pointer */
	mov x30, #0		/* clear link register */
	/* x19 is callee-saved; stash the kernel-supplied SP so we can
	 * re-read argc/argv/envp after the SP has been aligned and after
	 * __meuos_tls_init has been called (AAPCS64 gives it a fresh frame). */
	mov x19, sp
	/* argc = [x19], argv = x19+8, envp = argv + (argc+1)*8 */
	ldr x1, [x19]
	add x2, x19, #8		/* argv */
	add x3, x2, x1, lsl #3	/* &argv[argc] */
	add x3, x3, #8		/* envp = &argv[argc] + 1 */
	adrp x4, environ
	str x3, [x4, :lo12:environ]
	/* Align SP for the AAPCS64 call below. */
	and sp, x19, #-16
	mov x0, x3		/* envp for __meuos_tls_init */
	bl __meuos_tls_init
	/* Reload argc/argv/envp from the saved kernel SP. */
	ldr x0, [x19]		/* argc */
	add x1, x19, #8		/* argv */
	add x2, x1, x0, lsl #3
	add x2, x2, #8		/* envp */
	bl main
	/* main's return value (x0) becomes exit()'s status argument.
	 * exit() runs atexit handlers and fflush(NULL) before the raw
	 * _exit syscall, so buffered printf output is not lost. */
	bl exit
	brk #0			/* unreachable */

.bss
.align 8
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0

.section .note.GNU-stack,"",@progbits
