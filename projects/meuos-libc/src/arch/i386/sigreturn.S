.text

/* i386 rt_sigreturn trampoline. */
.globl __meuos_restore_rt
.type __meuos_restore_rt,@function
__meuos_restore_rt:
	movl $173, %eax             /* __NR_rt_sigreturn */
	int $0x80
	ud2

.section .note.GNU-stack,"",@progbits
