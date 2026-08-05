.text
.globl Point_setii
Point_setii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	movl %edx, 4(%rdi)
	addq $16, %rsp
	ret
.type Point_setii, @function
.size Point_setii, .-Point_setii
.text
.globl Point_sum
Point_sum:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type Point_sum, @function
.size Point_sum, .-Point_sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $4, %edx
	movl $3, %esi
	leaq -32(%rbp), %rdi
	callq Point_setii@plt
	leaq -32(%rbp), %rdi
	callq Point_sum@plt
	movl %eax, -16(%rbp)
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
