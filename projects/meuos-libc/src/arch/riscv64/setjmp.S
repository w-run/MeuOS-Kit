.text

/* RISC-V 64 LP64D setjmp/longjmp.
 *
 * jmp_buf layout (26 unsigned long words, 208 bytes), matching the
 * unsigned long jmp_buf[26] declared in setjmp.h:
 *   [0-11]  s0-s11   (callee-saved GPRs)
 *   [12]    sp
 *   [13]    ra
 *   [14-25] fs0-fs11 (callee-saved FPRs, hard-float ABI)
 *
 * sigjmp_buf appends (matching unsigned long sigjmp_buf[28]):
 *   [26] mask-saved flag (offset 208)
 *   [27] sigset_t         (offset 216)
 */

/* int setjmp(jmp_buf env)  -- a0 = env
 * int _setjmp(jmp_buf env) -- alias, no signal mask */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	sd s0, 0(a0)
	sd s1, 8(a0)
	sd s2, 16(a0)
	sd s3, 24(a0)
	sd s4, 32(a0)
	sd s5, 40(a0)
	sd s6, 48(a0)
	sd s7, 56(a0)
	sd s8, 64(a0)
	sd s9, 72(a0)
	sd s10, 80(a0)
	sd s11, 88(a0)
	sd sp, 96(a0)
	sd ra, 104(a0)
	fsd fs0, 112(a0)
	fsd fs1, 120(a0)
	fsd fs2, 128(a0)
	fsd fs3, 136(a0)
	fsd fs4, 144(a0)
	fsd fs5, 152(a0)
	fsd fs6, 160(a0)
	fsd fs7, 168(a0)
	fsd fs8, 176(a0)
	fsd fs9, 184(a0)
	fsd fs10, 192(a0)
	fsd fs11, 200(a0)
	li a0, 0
	ret

/* _Noreturn void longjmp(jmp_buf env, int val) -- a0 = env, a1 = val */
.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	ld s0, 0(a0)
	ld s1, 8(a0)
	ld s2, 16(a0)
	ld s3, 24(a0)
	ld s4, 32(a0)
	ld s5, 40(a0)
	ld s6, 48(a0)
	ld s7, 56(a0)
	ld s8, 64(a0)
	ld s9, 72(a0)
	ld s10, 80(a0)
	ld s11, 88(a0)
	ld sp, 96(a0)
	ld ra, 104(a0)
	fld fs0, 112(a0)
	fld fs1, 120(a0)
	fld fs2, 128(a0)
	fld fs3, 136(a0)
	fld fs4, 144(a0)
	fld fs5, 152(a0)
	fld fs6, 160(a0)
	fld fs7, 168(a0)
	fld fs8, 176(a0)
	fld fs9, 184(a0)
	fld fs10, 192(a0)
	fld fs11, 200(a0)
	bnez a1, 1f
	li a1, 1
1:	mv a0, a1
	ret

/* int sigsetjmp(sigjmp_buf env, int savesigs) -- a0 = env, a1 = savesigs
 * Save callee-saved registers (same as setjmp) and, if savesigs is non-zero,
 * capture the current signal mask into env[27] and set env[26]=1. */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	sd s0, 0(a0)
	sd s1, 8(a0)
	sd s2, 16(a0)
	sd s3, 24(a0)
	sd s4, 32(a0)
	sd s5, 40(a0)
	sd s6, 48(a0)
	sd s7, 56(a0)
	sd s8, 64(a0)
	sd s9, 72(a0)
	sd s10, 80(a0)
	sd s11, 88(a0)
	sd sp, 96(a0)
	sd ra, 104(a0)
	fsd fs0, 112(a0)
	fsd fs1, 120(a0)
	fsd fs2, 128(a0)
	fsd fs3, 136(a0)
	fsd fs4, 144(a0)
	fsd fs5, 152(a0)
	fsd fs6, 160(a0)
	fsd fs7, 168(a0)
	fsd fs8, 176(a0)
	fsd fs9, 184(a0)
	fsd fs10, 192(a0)
	fsd fs11, 200(a0)

	mv t0, a0			/* preserve env across syscall */
	beqz a1, 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[27], 8)
	 * riscv64/asm-generic syscall 135, args a0-a3. */
	li a0, 0			/* how = SIG_BLOCK */
	li a1, 0			/* set = NULL */
	addi a2, t0, 216		/* oldset = &env[27] */
	li a3, 8			/* sigsetsize */
	li a7, 135
	ecall
	li a0, 1
	sd a0, 208(t0)		/* env[26] = 1 (mask saved) */
	li a0, 0
	ret
2:
	sd zero, 208(t0)		/* env[26] = 0 (no mask) */
	li a0, 0
	ret

/* _Noreturn void siglongjmp(sigjmp_buf env, int val) -- a0=env, a1=val
 * Restore the signal mask first (if saved), then registers. */
.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	mv t0, a0			/* env */
	mv t1, a1			/* val */
	bnez t1, 1f
	li t1, 1
1:
	ld a2, 208(t0)		/* env[26]: mask saved? */
	beqz a2, 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[27], NULL, 8) */
	li a0, 2			/* how = SIG_SETMASK */
	addi a1, t0, 216		/* set = &env[27] */
	li a2, 0			/* oldset = NULL */
	li a3, 8			/* sigsetsize */
	li a7, 135
	ecall
3:
	ld s0, 0(t0)
	ld s1, 8(t0)
	ld s2, 16(t0)
	ld s3, 24(t0)
	ld s4, 32(t0)
	ld s5, 40(t0)
	ld s6, 48(t0)
	ld s7, 56(t0)
	ld s8, 64(t0)
	ld s9, 72(t0)
	ld s10, 80(t0)
	ld s11, 88(t0)
	ld sp, 96(t0)
	ld ra, 104(t0)
	fld fs0, 112(t0)
	fld fs1, 120(t0)
	fld fs2, 128(t0)
	fld fs3, 136(t0)
	fld fs4, 144(t0)
	fld fs5, 152(t0)
	fld fs6, 160(t0)
	fld fs7, 168(t0)
	fld fs8, 176(t0)
	fld fs9, 184(t0)
	fld fs10, 192(t0)
	fld fs11, 200(t0)
	mv a0, t1			/* return value */
	ret

.section .note.GNU-stack,"",@progbits
