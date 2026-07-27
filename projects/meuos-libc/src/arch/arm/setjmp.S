.arm
.text

/* ARMv7 setjmp/longjmp (AAPCS + VFP hard-float).
 *
 * jmp_buf layout (26 words = 104 bytes), matching musl arm:
 *   [0..8]  r4,r5,r6,r7,r8,r9,r10,r11,lr  (9 int regs)
 *   [9]     sp
 *   [10..25] d8,d9,d10,d11,d12,d13,d14,d15 (8 doubles = 16 words)
 *
 * sigjmp_buf appends:
 *   [26] mask-saved flag (offset 104)
 *   [27] sigset_t         (offset 108)
 * giving 28 words / 112 bytes. */

/* int setjmp(jmp_buf env)  -- r0 = env */
.globl setjmp
.type setjmp,%function
setjmp:
.globl _setjmp
.type _setjmp,%function
_setjmp:
	stmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	str sp, [r0]		/* sp at offset 36 */
	add r0, r0, #4
	vstm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	mov r0, #0
	bx lr

/* _Noreturn void longjmp(jmp_buf env, int val) */
.globl longjmp
.type longjmp,%function
longjmp:
.globl _longjmp
.type _longjmp,%function
_longjmp:
	ldmia r0!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	ldr r2, [r0]		/* sp */
	mov sp, r2
	add r0, r0, #4
	vldm r0, {d8, d9, d10, d11, d12, d13, d14, d15}
	cmp r1, #0
	moveq r0, #1
	movne r0, r1
	bx lr

/* int sigsetjmp(sigjmp_buf env, int savesigs)  r0=env, r1=savesigs */
.globl sigsetjmp
.type sigsetjmp,%function
sigsetjmp:
	mov r12, r0		/* env in scratch reg (not in callee-saved set) */
	stmia r12!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	str sp, [r12]
	add r12, r12, #4
	vstm r12, {d8, d9, d10, d11, d12, d13, d14, d15}
	sub r12, r12, #40	/* rewind r12 to env[0] (40 = 36+4) */

	cmp r1, #0
	beq 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[26], 8) */
	mov r0, #0		/* how = SIG_BLOCK */
	mov r1, #0		/* set = NULL */
	add r2, r12, #104	/* oldset = &env[26] */
	mov r3, #8		/* sigsetsize */
	mov r7, #175		/* SYS_rt_sigprocmask */
	svc #0

	mov r0, #1
	str r0, [r12, #104]	/* env[26] = 1 */
	mov r0, #0
	bx lr
2:	mov r0, #0
	str r0, [r12, #104]	/* env[26] = 0 */
	mov r0, #0
	bx lr

/* _Noreturn void siglongjmp(sigjmp_buf env, int val)  r0=env, r1=val
 * Save val on current stack BEFORE register restore.  After integer/VFP
 * register restore (but BEFORE sp restore), pop val from old stack, THEN
 * restore sp. */
.globl siglongjmp
.type siglongjmp,%function
siglongjmp:
	push {r1, lr}		/* save val; lr keeps stack balanced */
	mov r12, r0		/* stash env (syscall below clobbers r0) */
	ldr r2, [r12, #104]	/* env[26]: mask saved? */
	cmp r2, #0
	beq 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[27], NULL, 8)
	 *   r0=how, r1=set, r2=oldset, r3=sigsetsize, r7=175 */
	mov r0, #2
	add r1, r12, #108
	mov r2, #0
	mov r3, #8
	mov r7, #175
	svc #0
3:
	/* Restore integer regs (r4-r11, lr).  r12 keeps env. */
	ldmia r12!, {r4, r5, r6, r7, r8, r9, r10, r11, lr}
	ldr r2, [r12]		/* saved sp — do NOT restore yet */
	add r12, r12, #4
	vldm r12, {d8, d9, d10, d11, d12, d13, d14, d15}
	pop {r0}		/* val from OLD stack (sp hasn't changed) */
	mov sp, r2		/* NOW restore SP */
	cmp r0, #0
	moveq r0, #1
	bx lr

.section .note.GNU-stack,"",%progbits
