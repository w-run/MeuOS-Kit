.text
rewrite:
	endbr64
	subq $32, %rsp
	movq %rsi, 24(%rsp)
	movq %rdi, 16(%rsp)
	movq %rdi, 0(%rsp)
	movq %rsi, 8(%rsp)
	movl 4(%rsp), %eax
	movl 0(%rsp), %ecx
	movl %ecx, %edx
	shll $2, %edx
	shrl $2, %edx
	addl %edx, %eax
	movl %eax, 4(%rsp)
	movl 8(%rsp), %eax
	shrl $30, %ecx
	xorl %ecx, %eax
	movl %eax, 8(%rsp)
	movl 12(%rsp), %eax
	addl $1, %eax
	movl %eax, 12(%rsp)
	movq 0(%rsp), %rax
	movq 8(%rsp), %rdx
	addq $32, %rsp
	ret
.type rewrite, @function
.size rewrite, .-rewrite
.data
.balign 4
initial:
	.int 3221225479
	.int 10
	.int 8
	.int 9
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movq initial(%rip), %rdi
	leaq initial(%rip), %rax
	addq $8, %rax
	movq (%rax), %rsi
	callq rewrite
	movq %rax, -24(%rbp)
	movq %rdx, -32(%rbp)
	movq %rax, -48(%rbp)
	movq %rdx, -40(%rbp)
	movl -48(%rbp), %esi
	movl -44(%rbp), %edx
	movl -40(%rbp), %ecx
	movl -36(%rbp), %eax
	movl %esi, %edi
	shll $2, %edi
	shrl $2, %edi
	cmpl $7, %edi
	jnz .Lbb8
	shrl $30, %esi
	cmpl $3, %esi
	jnz .Lbb8
	cmpl $17, %edx
	jnz .Lbb8
	cmpl $11, %ecx
	jnz .Lbb8
	cmpl $10, %eax
	jnz .Lbb8
	movl $0, %eax
	jmp .Lbb9
.Lbb8:
	movl $1, %eax
.Lbb9:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
