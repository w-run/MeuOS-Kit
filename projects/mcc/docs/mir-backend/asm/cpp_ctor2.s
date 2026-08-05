.text
.globl Point_Pointii
Point_Pointii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	movl %edx, 4(%rdi)
	addq $16, %rsp
	ret
.type Point_Pointii, @function
.size Point_Pointii, .-Point_Pointii
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $4, %edx
	movl $3, %esi
	leaq -16(%rbp), %rdi
	callq Point_Pointii@plt
	movl -16(%rbp), %eax
	movl -12(%rbp), %ecx
	addl %ecx, %eax
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
