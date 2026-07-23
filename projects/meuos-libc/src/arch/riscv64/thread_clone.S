.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 *   a0 = start, a1 = arg, a2 = stack_top, a3 = control, a4 = tls
 *
 * Build a child-only call frame on the child stack, then invoke clone(2).
 *
 * riscv64 clone(flags, child_stack, ptid, tls, ctid):
 *   a0 = flags, a1 = child_stack, a2 = ptid, a3 = tls, a4 = ctid
 * Note the argument order differs from x86_64.  CLONE_SETTLS makes the
 * kernel install tls into tp (x4) directly, so the child does not need a
 * manual tp step (unlike aarch64's msr).
 *
 * Flags: CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
 *        PARENT_SETTID|CHILD_CLEARTID|CHILD_SETTID = 0x013d0f00
 * (identical to the x86_64/aarch64 set; riscv64 honors CLONE_SETTLS via a3). */
.globl __meuos_thread_clone
.type __meuos_thread_clone,@function
__meuos_thread_clone:
	/* Build the child frame: 32 bytes (4 words), 16-byte aligned. */
	addi a2, a2, -32
	sd a0, 0(a2)		/* child[0] = start */
	sd a1, 8(a2)		/* child[1] = arg */
	sd a3, 16(a2)		/* child[2] = control */
	sd a4, 24(a2)		/* child[3] = tls */

	/* clone(flags=0x013d0f00, child_stack=a2, ptid=control,
	 *      tls=tls, ctid=control) */
	li a0, 0x013d0f00
	mv a1, a2		/* child_stack */
	mv a5, a3		/* save control (a3 is clobbered below) */
	mv a2, a5		/* ptid = control */
	mv a3, a4		/* tls */
	mv a4, a5		/* ctid = control */
	li a7, 220		/* SYS_clone (riscv64/asm-generic) */
	ecall
	bnez a0, 1f		/* parent returns child tid / error */

	/* ---- child: a0 == 0, sp == child_stack ---- */
	ld a0, 0(sp)		/* start */
	ld a1, 8(sp)		/* arg */
	ld a2, 16(sp)		/* control */
	jalr a0			/* start(arg) -> result in a0 */
	mv a1, a2		/* control */
	call __meuos_thread_finish
	/* unreachable: __meuos_thread_finish is _Noreturn */
1:
	ret

.section .note.GNU-stack,"",@progbits
