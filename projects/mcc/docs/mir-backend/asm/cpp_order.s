.text
.globl Box_init
Box_init:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	imull %ecx, %eax
	movl %eax, 8(%rdi)
	addq $16, %rsp
	ret
.type Box_init, @function
.size Box_init, .-Box_init
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $3, -16(%rbp)
	movl $4, -12(%rbp)
	leaq -16(%rbp), %rdi
	callq Box_init@plt
	movl -8(%rbp), %eax
	subl $12, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
