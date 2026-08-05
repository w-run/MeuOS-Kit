.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $0, -48(%rbp)
	movl $42, -32(%rbp)
	movl $5, %esi
	leaq -32(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, %esi
	movl %esi, -12(%rbp)
	movl $5, %edx
	leaq -48(%rbp), %rdi
	callq __atomic_store_4@plt
	movl $5, %edx
	movl $1, %esi
	leaq -48(%rbp), %rdi
	callq __atomic_fetch_add_4@plt
	movl $5, %esi
	leaq -48(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -16(%rbp)
	cmpl $43, %eax
	movl $1, %ecx
	movl $0, %eax
	cmovnz %ecx, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
