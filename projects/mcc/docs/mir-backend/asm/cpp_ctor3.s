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
.globl Point_Point
Point_Point:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $0, 4(%rdi)
	addq $16, %rsp
	ret
.type Point_Point, @function
.size Point_Point, .-Point_Point
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
	leaq -32(%rbp), %rdi
	callq Point_Point@plt
	leaq -16(%rbp), %rdi
	callq Point_Point@plt
	leaq -32(%rbp), %rdi
	callq Point_sum@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
