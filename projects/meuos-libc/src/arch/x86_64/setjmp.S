.text

/* int setjmp(jmp_buf env)  -- rdi = env
 * int _setjmp(jmp_buf env) -- alias, no signal mask
 * Buffer layout (unsigned long words):
 *   [0] rbx  [1] rbp  [2] r12  [3] r13
 *   [4] r14  [5] r15  [6] rip  [7] rsp
 */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	movq %rbx,  0(%rdi)
	movq %rbp,  8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	movq (%rsp), %rax		/* return address (rip) */
	movq %rax, 48(%rdi)
	leaq 8(%rsp), %rax		/* caller's rsp */
	movq %rax, 56(%rdi)
	xorl %eax, %eax			/* return 0 */
	ret

/* _Noreturn void longjmp(jmp_buf env, int val)  -- rdi = env, rsi = val
 * _Noreturn void _longjmp(jmp_buf env, int val) -- alias
 */
.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	movl %esi, %eax			/* return value */
	testl %eax, %eax
	jnz 1f
	movl $1, %eax
1:
	movq  0(%rdi), %rbx
	movq  8(%rdi), %rbp
	movq 16(%rdi), %r12
	movq 24(%rdi), %r13
	movq 32(%rdi), %r14
	movq 40(%rdi), %r15
	movq 48(%rdi), %rdx		/* saved rip */
	movq 56(%rdi), %rsp		/* saved rsp */
	jmp *%rdx			/* jump to return address */

/* int sigsetjmp(sigjmp_buf env, int savesigs) -- rdi = env, rsi = savesigs
 *
 * Implemented in assembly (not as a C wrapper around setjmp) so that the
 * saved rsp points to the CALLER's frame, which remains valid as long as
 * the caller is on the stack when siglongjmp is invoked.  A C wrapper
 * would save a pointer to its own deallocated frame instead.
 *
 * sigjmp_buf layout:
 *   [0..7]  jmp_buf (same layout as above)
 *   [8]     mask-saved flag
 *   [9]     saved sigset_t
 */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	/* Save callee-saved registers and return context, exactly like
	 * setjmp.  The saved rsp is the caller's frame pointer. */
	movq %rbx,  0(%rdi)
	movq %rbp,  8(%rdi)
	movq %r12, 16(%rdi)
	movq %r13, 24(%rdi)
	movq %r14, 32(%rdi)
	movq %r15, 40(%rdi)
	movq (%rsp), %rax
	movq %rax, 48(%rdi)
	leaq 8(%rsp), %rax
	movq %rax, 56(%rdi)

	testq %rsi, %rsi		/* savesigs? */
	jz 2f

	/* rt_sigprocmask(SIG_BLOCK=0, NULL, &env[9], 8) to capture the
	 * current mask.  The syscall clobbers rax, rcx, r11; preserve env
	 * (rdi) on the stack. */
	pushq %rdi
	xorl %edi, %edi			/* how = SIG_BLOCK */
	xorl %esi, %esi			/* set = NULL */
	movq (%rsp), %rdx		/* rdx = env */
	leaq 72(%rdx), %rdx		/* rdx = &env[9] = oldset */
	movl $8, %r10d			/* sigsetsize */
	movl $14, %eax			/* SYS_rt_sigprocmask */
	syscall
	popq %rdi
	movq $1, 64(%rdi)		/* env[8] = 1 (mask saved) */
	xorl %eax, %eax			/* return 0 */
	ret
2:
	movq $0, 64(%rdi)		/* env[8] = 0 (no mask) */
	xorl %eax, %eax			/* return 0 */
	ret

/* _Noreturn void siglongjmp(sigjmp_buf env, int val) -- rdi=env, rsi=val */
.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	/* Stash the return value in a callee-saved register that we are
	 * about to overwrite from env anyway; the sigprocmask syscall below
	 * clobbers rax, so val cannot live there across it. */
	movl %esi, %r12d		/* r12 = val */
	testl %r12d, %r12d
	jnz 1f
	movl $1, %r12d
1:
	cmpq $0, 64(%rdi)		/* mask saved? */
	jz 3f

	/* rt_sigprocmask(SIG_SETMASK=2, &env[9], NULL, 8) */
	pushq %rdi			/* preserve env across syscall */
	movl $2, %edi			/* how = SIG_SETMASK */
	movq (%rsp), %rsi		/* rsi = env */
	leaq 72(%rsi), %rsi		/* rsi = &env[9] = set */
	xorl %edx, %edx			/* oldset = NULL */
	movl $8, %r10d			/* sigsetsize */
	movl $14, %eax			/* SYS_rt_sigprocmask */
	syscall
	popq %rdi
3:
	movl %r12d, %eax		/* return value; must precede r12 restore */
	movq  0(%rdi), %rbx
	movq  8(%rdi), %rbp
	movq 16(%rdi), %r12		/* env[2]: real r12 (val temp now consumed) */
	movq 24(%rdi), %r13
	movq 32(%rdi), %r14
	movq 40(%rdi), %r15
	movq 48(%rdi), %rdx		/* saved rip */
	movq 56(%rdi), %rsp		/* saved rsp */
	jmp *%rdx

.section .note.GNU-stack,"",@progbits
