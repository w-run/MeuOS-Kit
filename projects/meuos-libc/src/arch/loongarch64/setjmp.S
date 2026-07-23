.text

/* LoongArch64 LP64D setjmp/longjmp.
 *
 * jmp_buf layout (23 unsigned long words, 184 bytes), matching the
 * unsigned long jmp_buf[23] declared in setjmp.h:
 *   [0-8]   $s0-$s8    (callee-saved GPRs r23-r31, 9 words)
 *   [9]     $fp        (r22, callee-saved frame pointer)
 *   [10]    $sp        (r3)
 *   [11]    $ra        (r1)
 *   [12-19] $fs0-$fs7  (callee-saved FPRs f24-f31, 8 words)
 *   [20-22] reserved   (3 padding words)
 *
 * sigjmp_buf appends (matching unsigned long sigjmp_buf[25]):
 *   [23] mask-saved flag (offset 184)
 *   [24] sigset_t        (offset 192, 8 bytes saved by rt_sigprocmask)
 */

/* int setjmp(jmp_buf env)  -- $a0 = env
 * int _setjmp(jmp_buf env) -- alias, no signal mask */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	st.d $s0, $a0, 0
	st.d $s1, $a0, 8
	st.d $s2, $a0, 16
	st.d $s3, $a0, 24
	st.d $s4, $a0, 32
	st.d $s5, $a0, 40
	st.d $s6, $a0, 48
	st.d $s7, $a0, 56
	st.d $s8, $a0, 64
	st.d $fp, $a0, 72
	st.d $sp, $a0, 80
	st.d $ra, $a0, 88
	fst.d $fs0, $a0, 96
	fst.d $fs1, $a0, 104
	fst.d $fs2, $a0, 112
	fst.d $fs3, $a0, 120
	fst.d $fs4, $a0, 128
	fst.d $fs5, $a0, 136
	fst.d $fs6, $a0, 144
	fst.d $fs7, $a0, 152
	li.w $a0, 0
	jr $ra

/* _Noreturn void longjmp(jmp_buf env, int val) -- $a0 = env, $a1 = val */
.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	ld.d $s0, $a0, 0
	ld.d $s1, $a0, 8
	ld.d $s2, $a0, 16
	ld.d $s3, $a0, 24
	ld.d $s4, $a0, 32
	ld.d $s5, $a0, 40
	ld.d $s6, $a0, 48
	ld.d $s7, $a0, 56
	ld.d $s8, $a0, 64
	ld.d $fp, $a0, 72
	ld.d $sp, $a0, 80
	ld.d $ra, $a0, 88
	fld.d $fs0, $a0, 96
	fld.d $fs1, $a0, 104
	fld.d $fs2, $a0, 112
	fld.d $fs3, $a0, 120
	fld.d $fs4, $a0, 128
	fld.d $fs5, $a0, 136
	fld.d $fs6, $a0, 144
	fld.d $fs7, $a0, 152
	bnez $a1, 1f
	li.w $a1, 1
1:
	move $a0, $a1
	jr $ra

/* int sigsetjmp(sigjmp_buf env, int savesigs) -- $a0 = env, $a1 = savesigs
 * Save callee-saved registers (same as setjmp) and, if savesigs is non-zero,
 * capture the current signal mask into env[24] and set env[23]=1. */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	move $t0, $a0			/* preserve env across syscall */
	st.d $s0, $t0, 0
	st.d $s1, $t0, 8
	st.d $s2, $t0, 16
	st.d $s3, $t0, 24
	st.d $s4, $t0, 32
	st.d $s5, $t0, 40
	st.d $s6, $t0, 48
	st.d $s7, $t0, 56
	st.d $s8, $t0, 64
	st.d $fp, $t0, 72
	st.d $sp, $t0, 80
	st.d $ra, $t0, 88
	fst.d $fs0, $t0, 96
	fst.d $fs1, $t0, 104
	fst.d $fs2, $t0, 112
	fst.d $fs3, $t0, 120
	fst.d $fs4, $t0, 128
	fst.d $fs5, $t0, 136
	fst.d $fs6, $t0, 144
	fst.d $fs7, $t0, 152

	beqz $a1, 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[24], 8)
	 * loongarch64 asm-generic syscall 135, args $a0-$a3. */
	li.w $a0, 0			/* how = SIG_BLOCK */
	li.w $a1, 0			/* set = NULL */
	addi.d $a2, $t0, 192		/* oldset = &env[24] */
	li.w $a3, 8			/* sigsetsize */
	li.w $a7, 135
	syscall 0
	li.w $a0, 1
	st.d $a0, $t0, 184		/* env[23] = 1 (mask saved) */
	li.w $a0, 0
	jr $ra
2:
	st.d $zero, $t0, 184		/* env[23] = 0 (no mask) */
	li.w $a0, 0
	jr $ra

/* _Noreturn void siglongjmp(sigjmp_buf env, int val) -- $a0=env, $a1=val
 * Restore the signal mask first (if saved), then registers. */
.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	move $t0, $a0			/* env */
	move $t1, $a1			/* val */
	bnez $t1, 1f
	li.w $t1, 1
1:
	ld.d $a2, $t0, 184		/* env[23]: mask saved? */
	beqz $a2, 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[24], NULL, 8) */
	li.w $a0, 2			/* how = SIG_SETMASK */
	addi.d $a1, $t0, 192		/* set = &env[24] */
	li.w $a2, 0			/* oldset = NULL */
	li.w $a3, 8			/* sigsetsize */
	li.w $a7, 135
	syscall 0
3:
	ld.d $s0, $t0, 0
	ld.d $s1, $t0, 8
	ld.d $s2, $t0, 16
	ld.d $s3, $t0, 24
	ld.d $s4, $t0, 32
	ld.d $s5, $t0, 40
	ld.d $s6, $t0, 48
	ld.d $s7, $t0, 56
	ld.d $s8, $t0, 64
	ld.d $fp, $t0, 72
	ld.d $sp, $t0, 80
	ld.d $ra, $t0, 88
	fld.d $fs0, $t0, 96
	fld.d $fs1, $t0, 104
	fld.d $fs2, $t0, 112
	fld.d $fs3, $t0, 120
	fld.d $fs4, $t0, 128
	fld.d $fs5, $t0, 136
	fld.d $fs6, $t0, 144
	fld.d $fs7, $t0, 152
	move $a0, $t1
	jr $ra

.section .note.GNU-stack,"",@progbits
