.arm
.text

/* ARM EABI 64-bit divmod wrappers.
 *
 * __aeabi_uldivmod(n_lo:r0, n_hi:r1, d_lo:r2, d_hi:r3)
 *   -> q_lo:r0, q_hi:r1, r_lo:r2, r_hi:r3
 *
 * __aeabi_ldivmod(n_lo:r0, n_hi:r1, d_lo:r2, d_hi:r3)
 *   -> q_lo:r0, q_hi:r1, r_lo:r2, r_hi:r3
 *
 * Both delegate to C helpers (udivmod64, divmod64) that use pure-32-bit
 * operations — no recursive __aeabi emission.  The C functions return
 * quotient in r0,r1 and store remainder via the pointer argument (r4). */

.globl __aeabi_uldivmod
.type __aeabi_uldivmod,%function
__aeabi_uldivmod:
	/* n(r0,r1), d(r2,r3); the remainder pointer (3rd C argument)
	 * goes on the stack after the four register words. */
	push	{r4, lr}
	sub	sp, sp, #8		/* 8 bytes for remainder storage */
	mov	r4, sp			/* rem ptr */
	str	r4, [sp]		/* pass rem_ptr to udivmod64 */
	bl	udivmod64		/* q in r0,r1; rem at [r4] */
	ldr	r2, [r4]		/* rem low */
	ldr	r3, [r4, #4]		/* rem high */
	add	sp, sp, #8
	pop	{r4, lr}
	bx	lr

.globl __aeabi_ldivmod
.type __aeabi_ldivmod,%function
__aeabi_ldivmod:
	push	{r4, lr}
	sub	sp, sp, #8
	mov	r4, sp
	str	r4, [sp]		/* pass rem_ptr to divmod64 */
	bl	divmod64		/* q in r0,r1; rem at [r4] */
	ldr	r2, [r4]
	ldr	r3, [r4, #4]
	add	sp, sp, #8
	pop	{r4, lr}
	bx	lr

.section .note.GNU-stack,"",%progbits
