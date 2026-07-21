.text

/* AArch64 setjmp/longjmp.
 *
 * jmp_buf layout (22 unsigned long words, 176 bytes), matching musl:
 *   [0]  x19   [1]  x20   [2]  x21   [3]  x22
 *   [4]  x23   [5]  x24   [6]  x25   [7]  x26
 *   [8]  x27   [9]  x28   [10] x29(fp)  [11] x30(lr)
 *   [12] (reserved)       [13] sp
 *   [14] d8    [15] d9    [16] d10   [17] d11
 *   [18] d12   [19] d13   [20] d14   [21] d15
 *
 * sigjmp_buf appends:
 *   [22] mask-saved flag (offset 176)
 *   [23] sigset_t         (offset 184)
 * giving 24 words / 192 bytes. */

/* int setjmp(jmp_buf env)  -- x0 = env
 * int _setjmp(jmp_buf env) -- alias, no signal mask */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	stp x19, x20, [x0, #0]
	stp x21, x22, [x0, #16]
	stp x23, x24, [x0, #32]
	stp x25, x26, [x0, #48]
	stp x27, x28, [x0, #64]
	stp x29, x30, [x0, #80]
	mov x2, sp
	str x2, [x0, #104]
	stp d8,  d9,  [x0, #112]
	stp d10, d11, [x0, #128]
	stp d12, d13, [x0, #144]
	stp d14, d15, [x0, #160]
	mov x0, #0
	ret

/* _Noreturn void longjmp(jmp_buf env, int val) -- x0 = env, x1 = val */
.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	ldp x19, x20, [x0, #0]
	ldp x21, x22, [x0, #16]
	ldp x23, x24, [x0, #32]
	ldp x25, x26, [x0, #48]
	ldp x27, x28, [x0, #64]
	ldp x29, x30, [x0, #80]
	ldr x2, [x0, #104]
	mov sp, x2
	ldp d8,  d9,  [x0, #112]
	ldp d10, d11, [x0, #128]
	ldp d12, d13, [x0, #144]
	ldp d14, d15, [x0, #160]
	cmp w1, #0
	csinc w0, w1, wzr, ne	/* val != 0 ? val : 1 */
	ret

/* int sigsetjmp(sigjmp_buf env, int savesigs) -- x0 = env, x1 = savesigs
 *
 * Save callee-saved registers (same as setjmp) and, if savesigs is non-zero,
 * capture the current signal mask into env[23] and set env[22]=1.
 * x9 is caller-saved and not touched by the svc path, so it safely holds env
 * across the rt_sigprocmask call. */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	stp x19, x20, [x0, #0]
	stp x21, x22, [x0, #16]
	stp x23, x24, [x0, #32]
	stp x25, x26, [x0, #48]
	stp x27, x28, [x0, #64]
	stp x29, x30, [x0, #80]
	mov x2, sp
	str x2, [x0, #104]
	stp d8,  d9,  [x0, #112]
	stp d10, d11, [x0, #128]
	stp d12, d13, [x0, #144]
	stp d14, d15, [x0, #160]

	mov x9, x0		/* preserve env across syscall */
	cbz x1, 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[23], 8)
	 * aarch64 syscall 135, args x0-x3, clobbers x0-x4/x8. */
	mov x0, #0		/* how = SIG_BLOCK */
	mov x1, #0		/* set = NULL */
	add x2, x9, #184	/* oldset = &env[23] */
	mov x3, #8		/* sigsetsize */
	mov x8, #135
	svc #0
	mov x0, x9
	mov x1, #1
	str x1, [x0, #176]	/* env[22] = 1 (mask saved) */
	mov x0, #0
	ret
2:
	str xzr, [x9, #176]	/* env[22] = 0 (no mask) */
	mov x0, #0
	ret

/* _Noreturn void siglongjmp(sigjmp_buf env, int val) -- x0=env, x1=val
 * Restore the signal mask first (via svc, which only clobbers x0-x4/x8),
 * then restore registers.  x9 holds env, x10 holds val across the call. */
.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	mov x9, x0		/* env */
	mov x10, x1		/* val */
	cmp x10, #0
	csinc x10, x10, xzr, ne	/* val != 0 ? val : 1 */

	ldr x1, [x9, #176]	/* env[22]: mask saved? */
	cbz x1, 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[23], NULL, 8) */
	mov x0, #2		/* how = SIG_SETMASK */
	add x1, x9, #184	/* set = &env[23] */
	mov x2, #0		/* oldset = NULL */
	mov x3, #8		/* sigsetsize */
	mov x8, #135
	svc #0
3:
	mov x0, x9		/* env */
	ldp x19, x20, [x0, #0]
	ldp x21, x22, [x0, #16]
	ldp x23, x24, [x0, #32]
	ldp x25, x26, [x0, #48]
	ldp x27, x28, [x0, #64]
	ldp x29, x30, [x0, #80]
	ldr x2, [x0, #104]
	mov sp, x2
	ldp d8,  d9,  [x0, #112]
	ldp d10, d11, [x0, #128]
	ldp d12, d13, [x0, #144]
	ldp d14, d15, [x0, #160]
	mov x0, x10		/* return value */
	ret

.section .note.GNU-stack,"",@progbits
