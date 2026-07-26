.arm
.text

/* long __meuos_syscall6(number, a1, a2, a3, a4, a5, a6)
 * Translate AAPCS to the Linux ARM EABI syscall ABI:
 *   syscall number in r7, arguments in r0-r5, enter via svc #0.
 *
 * ARM AAPCS passes first 4 args in r0-r3; a4/a5/a6 on stack.
 *
 * Entry:   r0=num, r1=a1, r2=a2, r3=a3, [sp]=a4, [sp+4]=a5, [sp+8]=a6
 * Target:  r7=num, r0=a1, r1=a2, r2=a3, r3=a4, r4=a5, r5=a6 */

.globl __meuos_syscall6
.type __meuos_syscall6,%function
__meuos_syscall6:
	push {r4, r5, r7, lr}
	/* After push +16, stack args at [sp+16],[sp+20],[sp+24] */
	mov r7, r0		/* save number now (r0 is about to be reused) */
	ldr r0, [sp, #16]	/* a4 */
	ldr r4, [sp, #20]	/* a5 */
	ldr r5, [sp, #24]	/* a6 */
	/* r0 = a4, r1 = a1, r2 = a2, r3 = a3, r4 = a5, r5 = a6
	 * Need: r7=num, r0=a1, r1=a2, r2=a3, r3=a4, r4=a5, r5=a6
	 * Swap a1 <-> a4: r0 has a4, need to put a1 in r0.
	 * Currently: r1=a1, r2=a2, r3=a3, r0=a4.
	 * Solution: mov r3, r0 (a3->r3 done), then get a1 from r1 into r0.
	 * But we need a3 in r2... be careful.
	 * Better approach: save all regs to temp then reorder. */
	/* Order: {r7=num, [r0=a4,r1=a1,r2=a2,r3=a3]}. Move to syscall layout */
	/* Step 1: r3 = a4 (what goes in r3 for syscall) */
	/* r0 currently = a4, so we need to move a4 to r3, a1 to r0 */
	/* Save a3 first (we need it in r2) */
	mov lr, r3		/* lr = a3 (temp) */
	mov r3, r0		/* r3 = a4 (syscall arg3) */
	mov r0, r1		/* r0 = a1 (syscall arg0) */
	mov r1, r2		/* r1 = a2 (syscall arg1) */
	mov r2, lr		/* r2 = a3 (syscall arg2) */
	/* r4 = a5, r5 = a6 already in place */

	svc #0

	pop {r4, r5, r7, lr}
	bx lr

.section .note.GNU-stack,"",%progbits
