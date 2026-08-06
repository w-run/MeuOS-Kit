.arm
.text

/* Linux ARM EABI (armv7) process entry.  The kernel supplies argc at [sp]
 * followed by argv[] (32-bit pointers) and envp[].  AAPCS requires SP to be
 * 8-byte aligned at function call boundaries.  Set up TLS (c13/TPIDRURO)
 * before handing control to the C library so that _Thread_local variables
 * work.
 *
 * All the startup work -- capturing argv[0]/auxv, running the
 * .preinit_array/.init_array constructors, registering the .fini_array walk
 * with atexit(), calling main() and passing its status to exit() -- lives in
 * the portable __libc_start_main() (src/startup.c), so this stub only
 * marshals arguments.  Its GNU signature is
 *   __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
 * so r0-r3 carry the first four and the remaining three go on the stack.
 *
 * r4 is callee-saved; stash the kernel-supplied SP there so we can re-read
 * argc/argv after alignment and __meuos_tls_init. */

.globl _start
.type _start,%function
_start:
	mov r11, #0		/* clear frame pointer */
	mov lr, #0		/* clear link register */
	mov r4, sp		/* stash kernel sp */

	/* argc = [r4], argv = r4+4, envp = argv + (argc+1)*4 */
	ldr r0, [r4]		/* argc */
	add r1, r4, #4		/* argv */
	add r2, r1, r0, lsl #2	/* &argv[argc] */
	add r2, r2, #4		/* envp = &argv[argc] + 1 */

	ldr r3, =environ
	str r2, [r3]

	/* 8-byte align SP for AAPCS calls */
	bic sp, r4, #7

	mov r0, r2		/* envp for __meuos_tls_init */
	bl __meuos_tls_init
	/* r0 = TLS base pointer; install into c13/TPIDRURO via mcr. */
	bl __meuos_set_tls

	/* __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end).
	 *
	 * init = __libc_csu_init: walks .preinit_array, calls _init(),
	 * then walks .init_array so compat archives have a named entry
	 * point to hook into startup.
	 * fini = __libc_csu_fini: walks .fini_array in reverse and calls
	 * _fini(), registered with atexit so it runs on the exit() path.
	 * rtld_fini = NULL: we are statically linked, no dynamic loader.
	 *
	 * r0-r3 take main/argc/argv/init; fini, rtld_fini and stack_end go on
	 * the stack at [sp+0], [sp+4] and [sp+8].  A register list pushes in
	 * ascending register order, so name the slots r5 < r6 < r7 < r8 to get
	 * fini, rtld_fini, stack_end and a pad that keeps SP 8-byte aligned. */
	ldr r5, =__libc_csu_fini		/* fini      -> [sp+0] */
	mov r6, #0		/* rtld_fini -> [sp+4] */
	mov r7, r4		/* stack_end -> [sp+8] */
	mov r8, #0		/* pad, keeps SP 8-byte aligned */
	push {r5, r6, r7, r8}

	ldr r0, =main		/* main */
	ldr r1, [r4]		/* argc */
	add r2, r4, #4		/* argv */
	ldr r3, =__libc_csu_init
	bl __libc_start_main
	bkpt #0			/* unreachable: it exit()s */

.bss
.align 4
.globl environ
.type environ,%object
.size environ,4
environ:
	.space 4

.section .note.GNU-stack,"",%progbits
