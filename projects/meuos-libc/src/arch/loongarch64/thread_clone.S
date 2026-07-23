.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 *   $a0 = start, $a1 = arg, $a2 = stack_top, $a3 = control, $a4 = tls
 *
 * Build a child-only call frame on the child stack, then invoke clone(2).
 *
 * loongarch64 clone(flags, child_stack, ptid, tls, ctid):
 *   $a0 = flags, $a1 = child_stack, $a2 = ptid, $a3 = tls, $a4 = ctid
 * Note the argument order differs from x86_64.  CLONE_SETTLS makes the
 * kernel install tls into $tp (r21) directly, so the child does not need
 * a manual tp step (unlike aarch64's msr).
 *
 * Flags: CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
 *        PARENT_SETTID|CHILD_CLEARTID|CHILD_SETTID = 0x013d0f00
 * (identical to the x86_64/aarch64/riscv64 set; loongarch64 honors
 * CLONE_SETTLS via $a3). */

.globl __meuos_thread_clone
.type __meuos_thread_clone,@function
__meuos_thread_clone:
	/* Build the child frame: 32 bytes (4 words), 16-byte aligned. */
	addi.d $a2, $a2, -32
	st.d $a0, $a2, 0		/* child[0] = start */
	st.d $a1, $a2, 8		/* child[1] = arg */
	st.d $a3, $a2, 16		/* child[2] = control */
	st.d $a4, $a2, 24		/* child[3] = tls */

	/* clone(flags=0x013d0f00, child_stack=$a2, ptid=control,
	 *       tls=tls, ctid=control) */
	li.d $a0, 0x013d0f00
	move $a1, $a2		/* child_stack */
	move $t0, $a3		/* save control ($a3 clobbered below) */
	move $a2, $t0		/* ptid = control */
	move $a3, $a4		/* tls */
	move $a4, $t0		/* ctid = control */
	li.w $a7, 220		/* SYS_clone (asm-generic, matched in syscall.h) */
	syscall 0
	bnez $a0, 1f		/* parent returns child tid / error */

	/* ---- child: $a0 == 0, $sp == child_stack ---- */
	ld.d $a0, $sp, 0		/* start */
	ld.d $a1, $sp, 8		/* arg */
	jirl $ra, $a0, 0		/* start(arg) -> result in $a0 */
	ld.d $a1, $sp, 16		/* control */
	bl __meuos_thread_finish
	/* unreachable: __meuos_thread_finish is _Noreturn */
1:
	jr $ra

.section .note.GNU-stack,"",@progbits
