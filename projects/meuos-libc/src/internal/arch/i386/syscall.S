.text

/* long __syscall6(number, a1, a2, a3, a4, a5, a6)
 * Linux i386 uses int $0x80 with eax, ebx, ecx, edx, esi, edi, ebp. */
.globl __meuos_syscall6
.type __meuos_syscall6,@function
__meuos_syscall6:
	pushl %ebx
	pushl %esi
	pushl %edi
	pushl %ebp
	movl 20(%esp), %eax
	movl 24(%esp), %ebx
	movl 28(%esp), %ecx
	movl 32(%esp), %edx
	movl 36(%esp), %esi
	movl 40(%esp), %edi
	movl 44(%esp), %ebp
	int $0x80
	popl %ebp
	popl %edi
	popl %esi
	popl %ebx
	ret

.section .note.GNU-stack,"",@progbits
