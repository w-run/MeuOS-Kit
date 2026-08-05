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
