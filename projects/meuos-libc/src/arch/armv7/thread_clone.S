.arm
.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 *   r0 = start, r1 = arg, r2 = stack_top, r3 = control, [sp] = tls
 *
 * Build a child-only call frame on the child stack, then invoke clone(2).
 *
 * armv7 clone(flags, child_stack, ptid, tls, ctid):
 *   r0 = flags, r1 = child_stack, r2 = ptid, r3 = tls, r4 = ctid
 * Note the argument order: tls is r3 (not r4 like x86_64), and ctid is r4.
 * CLONE_SETTLS makes the kernel install tls into c13/TPIDRURO directly,
 * so the child does not need a manual __meuos_set_tls step.
 *
 * Flags: CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
 *        PARENT_SETTID = 0x001d0f00
 * (no CHILD_CLEARTID — __meuos_thread_finish manually sets tid=0 and does
 * FUTEX_WAKE, so the kernel-side write is redundant.  Avoiding
 * CHILD_CLEARTID works around qemu-user CLONE_THREAD bugs.)
 *
 * The libc's thread.c writes at the bottom of the allocated child stack:
 *   child_stack[-8] = arg
 *   child_stack[-4] = func
 * and passes child_stack-8 as the sp argument.  So when the child resumes,
 * [sp+0] = start, [sp+4] = arg (both stored by us in the frame below). */

.globl __meuos_thread_clone
.type __meuos_thread_clone,%function
__meuos_thread_clone:
	/* Build the child frame: 32 bytes (8 words for 32-bit armv7), 8-byte aligned. */
	sub r2, r2, #32
	str r0, [r2, #0]	/* child[0] = start */
	str r1, [r2, #4]	/* child[1] = arg */
	str r3, [r2, #8]	/* child[2] = control */
	ldr r12, [sp, #0]	/* r12 = tls (5th C arg from caller stack) */
	str r12, [r2, #12]	/* child[3] = tls */

	/* clone(flags=0x001d0f00, child_stack=r2, ptid=control, tls=tls, ctid=0) */
	mov r5, r3		/* save control (r3 clobbered below) */
	ldr r0, =0x001d0f00	/* clone flags (not an encodeable mov immediate) */
	mov r1, r2		/* child_stack */
	mov r2, r5		/* ptid = control (PARENT_SETTID) */
	mov r3, r12		/* tls */
	mov r4, #0		/* ctid = 0 (no CHILD_CLEARTID/SETTID) */
	mov r7, #120		/* SYS_clone (armv7 EABI) */
	svc #0
	cmp r0, #0
	bne 1f		/* parent returns child tid / error */

	/* ---- child: r0 == 0, sp == child_stack ---- */
	ldr r0, [sp, #0]	/* start */
	ldr r1, [sp, #4]	/* arg */
	ldr r2, [sp, #8]	/* control */
	blx r0			/* start(arg) -> result in r0 */
	mov r1, r2		/* control */
	bl __meuos_thread_finish
	bkpt #0			/* unreachable: __meuos_thread_finish is _Noreturn */

1:
	bx lr

.section .note.GNU-stack,"",%progbits
