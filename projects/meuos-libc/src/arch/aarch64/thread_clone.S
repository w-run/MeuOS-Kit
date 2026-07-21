.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 *   x0 = start, x1 = arg, x2 = stack_top, x3 = control, x4 = tls
 *
 * Build a child-only call frame on the child stack, then invoke clone(2).
 *
 * aarch64 clone(flags, child_stack, ptid, tls, ctid):
 *   x0 = flags, x1 = child_stack, x2 = ptid, x3 = tls, x4 = ctid
 * Note the argument order differs from x86_64 (tls is x3 here, not r8)
 * and from i386 (no user_desc indirection).  CLONE_SETTLS makes the
 * kernel install tls into TPIDR_EL0 directly, so the child does not
 * need a manual arch_prctl/__meuos_set_tls step.
 *
 * Flags: CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
 *        PARENT_SETTID|CHILD_CLEARTID|CHILD_SETTID = 0x013d0f00
 * (identical to the x86_64 set; aarch64 honors CLONE_SETTLS via x3). */
.globl __meuos_thread_clone
.type __meuos_thread_clone,@function
__meuos_thread_clone:
	/* Build the child frame: 32 bytes (4 words), 16-byte aligned. */
	sub x2, x2, #32
	stp x0, x1, [x2, #0]	/* child[0]=start, child[1]=arg */
	stp x3, x4, [x2, #16]	/* child[2]=control, child[3]=tls */

	/* clone(flags=0x013d0f00, child_stack=x2, ptid=control,
	 *      tls=tls, ctid=control) */
	mov x5, x3		/* save control (x3 is clobbered below) */
	movz x0, #0x0f00
	movk x0, #0x013d, lsl #16
	mov x1, x2		/* child_stack */
	mov x2, x5		/* ptid = control */
	mov x3, x4		/* tls */
	mov x4, x5		/* ctid = control */
	mov x8, #220		/* SYS_clone (aarch64) */
	svc #0
	cbnz x0, 1f		/* parent returns child tid */

	/* ---- child: x0 == 0, sp = child_stack ---- */
	ldp x9, x10, [sp, #0]	/* x9=start, x10=arg */
	ldp x11, x12, [sp, #16]	/* x11=control, x12=tls (unused: kernel set TPIDR_EL0) */
	mov x0, x10		/* arg */
	blr x9			/* start(arg) -> int result in x0 */
	mov x1, x11		/* control */
	bl __meuos_thread_finish
	brk #0			/* unreachable: __meuos_thread_finish is _Noreturn */
1:
	ret

.section .note.GNU-stack,"",@progbits
