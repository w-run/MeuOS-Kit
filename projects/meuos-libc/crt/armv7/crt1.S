.arm
.text

/* Linux ARM EABI (armv7) process entry.  The kernel supplies argc at [sp]
 * followed by argv[] (32-bit pointers) and envp[].  AAPCS requires SP to be
 * 8-byte aligned at function call boundaries.  Set up TLS (c13/TPIDRURO)
 * before calling main so that _Thread_local variables work, then terminate
 * via exit() so that atexit handlers run and stdio buffers are flushed.
 *
 * r4 is callee-saved; stash the kernel-supplied SP there so we can
 * re-read argc/argv/envp after alignment and __meuos_tls_init. */

.globl _start
.type _start,@function
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

	/* Reload argc/argv/envp from saved kernel SP */
	ldr r0, [r4]		/* argc */
	add r1, r4, #4		/* argv */
	add r2, r1, r0, lsl #2
	add r2, r2, #4		/* envp */

	bl main
	bl exit
	bkpt #0			/* unreachable */

.bss
.align 4
.globl environ
.type environ,@object
.size environ,4
environ:
	.space 4

.section .note.GNU-stack,"",@progbits
