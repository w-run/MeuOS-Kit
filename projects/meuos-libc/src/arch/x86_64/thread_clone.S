.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 * Build a child-only call frame before invoking clone(2).
 *
 * Register view at entry (System V):
 *   rdi = start   rsi = arg   rdx = stack_top
 *   rcx = control  r8 = tls
 */
.globl __meuos_thread_clone
.type __meuos_thread_clone,@function
__meuos_thread_clone:
	movq %rcx, %r11
	movq %r8, %r9
	subq $32, %rdx
	movq %rdi, 0(%rdx)
	movq %rsi, 8(%rdx)
	movq %r11, 16(%rdx)
	movq %r9, 24(%rdx)
	/* CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|SETTLS|
	 * PARENT_SETTID|CHILD_CLEARTID|CHILD_SETTID. */
	movq $0x013d0f00, %rdi
	movq %rdx, %rsi
	movq %r11, %rdx
	movq %r11, %r10
	movq %r9, %r8
	xorq %r8, %r8
	movq $56, %rax
	syscall
	testq %rax, %rax
	jnz 1f
	/* Child thread.  Restore start/arg/control/tls from the frame. */
	popq %r12		/* r12 = start */
	popq %rdi		/* rdi = arg (must survive arch_prctl below) */
	popq %rbx		/* rbx = control */
	popq %rsi		/* rsi = tls */
	testq %rsi, %rsi
	jz 2f
	/* arch_prctl(ARCH_SET_FS, tls) uses rdi and rsi, so stash the
	 * caller's argument in a callee-saved register that the syscall
	 * does not clobber, then restore it.  r13 is safe: the child has
	 * no caller frame to preserve and the trampoline will save r13
	 * itself if it needs it. */
	movq %rdi, %r13
	movq $0x1002, %rdi
	movq $158, %rax
	syscall
	testq %rax, %rax
	js 3f
	movq %r13, %rdi		/* restore arg for start(arg) */
2:
	call *%r12
	movq %rbx, %rsi
	movl %eax, %edi
	call __meuos_thread_finish
	ud2
3:
	movq $60, %rax
	movq $127, %rdi
	syscall
	ud2
1:
	ret

.section .note.GNU-stack,"",@progbits
