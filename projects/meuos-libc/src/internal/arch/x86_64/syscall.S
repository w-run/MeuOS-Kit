.text

/* long __syscall6(number, a1, a2, a3, a4, a5, a6)
 * Translate the System V call ABI to Linux x86_64's syscall registers. */
.globl __meuos_syscall6
.type __meuos_syscall6,@function
__meuos_syscall6:
	movq %rdi, %rax
	movq %rsi, %rdi
	movq %rdx, %rsi
	movq %rcx, %rdx
	movq %r8, %r10
	movq %r9, %r8
	movq 8(%rsp), %r9
	syscall
	ret

.section .note.GNU-stack,"",@progbits
