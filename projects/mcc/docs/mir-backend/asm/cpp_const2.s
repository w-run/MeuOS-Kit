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
.globl A_getK
A_getK:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type A_getK, @function
.size A_getK, .-A_getK
