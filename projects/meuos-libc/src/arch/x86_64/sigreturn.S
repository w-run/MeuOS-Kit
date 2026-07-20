.text

/* void __meuos_restore_rt(void)
 * Signal-return trampoline installed via sa_restorer + SA_RESTORER.  The
 * kernel jumps here when a signal handler returns; we must issue
 * rt_sigreturn to unwind the signal frame.  No prologue/epilogue: the
 * kernel-built frame on the stack is the argument. */
.globl __meuos_restore_rt
.type __meuos_restore_rt,@function
__meuos_restore_rt:
	movq $15, %rax		/* SYS_rt_sigreturn */
	syscall
	ud2

.section .note.GNU-stack,"",@progbits
