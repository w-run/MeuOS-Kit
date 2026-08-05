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
.globl Point_moveii
Point_moveii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	imull $2, %esi, %ecx
	addl %ecx, %eax
	movl %eax, (%rdi)
	movl 4(%rdi), %eax
	imull $2, %edx, %ecx
	addl %ecx, %eax
	movl %eax, 4(%rdi)
	addq $16, %rsp
	ret
.type Point_moveii, @function
.size Point_moveii, .-Point_moveii
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $2, %edx
	movl $1, %esi
	leaq -16(%rbp), %rdi
	callq Point_setii@plt
	movl $4, %edx
	movl $3, %esi
	leaq -16(%rbp), %rdi
	callq Point_moveii@plt
	movl -16(%rbp), %eax
	cmpl $7, %eax
	jnz .Lbb8
	movl -12(%rbp), %eax
	cmpl $10, %eax
	jnz .Lbb7
	movl $0, %eax
	jmp .Lbb9
.Lbb7:
	movl $2, %eax
	jmp .Lbb9
.Lbb8:
	movl $1, %eax
.Lbb9:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
