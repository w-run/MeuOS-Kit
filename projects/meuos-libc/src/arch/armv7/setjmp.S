.arm
.text

/* ARMv7 setjmp/longjmp (AAPCS + VFP hard-float).
 *
 * jmp_buf layout (26 words = 104 bytes), matching musl arm:
 *   [0]  r4    [1]  r5    [2]  r6    [3]  r7
 *   [4]  r8    [5]  r9    [6]  r10   [7]  r11(fp)
 *   [8]  r14(lr)           [9]  sp
 *   [10] d8    [11] d9    [12] d10   [13] d11
 *   [14] d12   [15] d13   [16] d14   [17] d15
 *   [18] d8(hi) [19] d9(hi)...  Wait — d8-d15 are doublewords (64 bits each),
 *   so they occupy 8 * 2 = 16 words (32-bit each).
 *   Integer: 10 words, VFP: 16 words = 26 total.
 *
 * sigjmp_buf appends:
 *   [26] mask-saved flag (offset 104)
 *   [27] sigset_t         (offset 108)
 * giving 28 words / 112 bytes. */

/* int setjmp(jmp_buf env)  -- r0 = env */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	/* Save integer callee-saved regs: r4-r11, lr */
	stmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	mov r1, sp
	str r1, [r0]		/* sp at offset 36 (9th word) */
	add r0, r0, #4
	/* Save VFP callee-saved: d8-d15 (8 doublewords = 16 words) */
	vstm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	mov r0, #0
	bx lr

/* _Noreturn void longjmp(jmp_buf env, int val) -- r0 = env, r1 = val */
.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	/* Restore integer regs.  r0 still points to env start. */
	ldmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	ldr r2, [r0]		/* sp */
	add r0, r0, #4
	mov sp, r2
	/* Restore VFP */
	vldm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	/* Return val != 0 ? val : 1 */
	cmp r1, #0
	moveq r0, #1
	movne r0, r1
	bx lr

/* int sigsetjmp(sigjmp_buf env, int savesigs) -- r0 = env, r1 = savesigs
 * Save registers (same as setjmp) and, if savesigs != 0, capture the
 * current signal mask.  r8 holds env across the svc call. */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	stmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	mov r2, sp
	str r2, [r0]
	add r0, r0, #4
	vstm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	sub r0, r0, #44		/* rewind to env start */

	/* Save env ptr, then check savesigs */
	mov r8, r0
	cmp r1, #0
	beq 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[26], 8)
	 * ARM syscall rt_sigprocmask = 175 (for arm32). But actually,
	 * r0-r3 are clobbered by syscall.  Save env in r8 first. */
	mov r0, #0		/* how = SIG_BLOCK */
	mov r1, #0		/* set = NULL */
	add r2, r8, #104	/* oldset = &env[26] (word offset 26 * 4) */
	mov r3, #8		/* sigsetsize */
	mov r7, #175		/* SYS_rt_sigprocmask (ARM) */
	svc #0
	mov r0, r8
	mov r1, #1
	str r1, [r0, #104]	/* env[26] = 1 (mask saved) */
	mov r0, #0
	bx lr
2:
	mov r1, #0
	str r1, [r8, #104]	/* env[26] = 0 (no mask) */
	mov r0, #0
	bx lr

/* _Noreturn void siglongjmp(sigjmp_buf env, int val) -- r0=env, r1=val */
.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	mov r8, r0		/* env */
	mov r9, r1		/* val */
	cmp r9, #0
	moveq r9, #1		/* val != 0 ? val : 1 */

	ldr r1, [r8, #104]	/* env[26]: mask saved? */
	cmp r1, #0
	beq 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[27], NULL, 8) */
	mov r0, #2		/* how = SIG_SETMASK */
	add r1, r8, #108	/* set = &env[27] */
	mov r2, #0		/* oldset = NULL */
	mov r3, #8		/* sigsetsize */
	mov r7, #175		/* SYS_rt_sigprocmask */
	svc #0
3:
	mov r0, r8		/* env start */
	ldmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	ldr r2, [r0]
	mov sp, r2
	add r0, r0, #4
	vldm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	mov r0, r9		/* return value */
	bx lr

.section .note.GNU-stack,"",@progbits
