.text

/* Linux aarch64 process entry.  The kernel supplies argc at [sp] followed by
 * argv[] and envp[].  AAPCS64 requires SP to be 16-byte aligned at function
 * call boundaries.  Set up TLS (TPIDR_EL0) before handing control to the C
 * library so that _Thread_local variables (notably errno) work.
 *
 * All the startup work -- capturing argv[0]/auxv, running the
 * .preinit_array/.init_array constructors, registering the .fini_array walk
 * with atexit(), calling main() and passing its status to exit() -- lives in
 * the portable __libc_start_main() (src/startup.c), so this stub only
 * marshals arguments.  Its GNU signature is
 *   __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
 * which fits entirely in x0-x6. */
.globl _start
.type _start,@function
_start:
	mov x29, #0		/* clear frame pointer */
	mov x30, #0		/* clear link register */
	/* x19 is callee-saved; stash the kernel-supplied SP so we can
	 * re-read argc/argv after the SP has been aligned and after
	 * __meuos_tls_init has been called (AAPCS64 gives it a fresh frame). */
	mov x19, sp
	/* argc = [x19], argv = x19+8, envp = argv + (argc+1)*8 */
	ldr x1, [x19]
	add x2, x19, #8		/* argv */
	add x3, x2, x1, lsl #3	/* &argv[argc] */
	add x3, x3, #8		/* envp = &argv[argc] + 1 */
	adrp x4, environ
	str x3, [x4, :lo12:environ]
	/* Align SP for the AAPCS64 calls below. */
	and sp, x19, #-16
	mov x0, x3		/* envp for __meuos_tls_init */
	bl __meuos_tls_init
	/* __libc_start_main(main, argc, argv, NULL, NULL, NULL, stack_end) */
	adrp x0, main
	add x0, x0, :lo12:main
	ldr x1, [x19]		/* argc */
	add x2, x19, #8		/* argv */
	mov x3, #0		/* init */
	mov x4, #0		/* fini */
	mov x5, #0		/* rtld_fini */
	mov x6, x19		/* stack_end */
	bl __libc_start_main
	brk #0			/* unreachable: it exit()s */

.bss
.align 8
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0

.section .note.GNU-stack,"",@progbits
