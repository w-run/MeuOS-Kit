.text
.globl A_A
A_A:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	addq $16, %rsp
	ret
.type A_A, @function
.size A_A, .-A_A
.text
.globl A_helperK
A_helperK:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type A_helperK, @function
.size A_helperK, .-A_helperK
.text
.globl A_getK
A_getK:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -8(%rbp)
	callq A_helperK@plt
	movl %eax, -16(%rbp)
	addl $1, %eax
	leave
	ret
.type A_getK, @function
.size A_getK, .-A_getK
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq A_A@plt
	leaq -32(%rbp), %rdi
	callq A_getK@plt
	movl %eax, -16(%rbp)
	subl $2, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
