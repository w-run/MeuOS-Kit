.text
.globl A_seti
A_seti:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type A_seti, @function
.size A_seti, .-A_seti
.text
.globl A_setii
A_setii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, %eax
	addl %edx, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type A_setii, @function
.size A_setii, .-A_setii
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $1, %esi
	leaq -16(%rbp), %rdi
	callq A_seti@plt
	movl $3, %edx
	movl $2, %esi
	leaq -16(%rbp), %rdi
	callq A_setii@plt
	movl -16(%rbp), %eax
	subl $5, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
