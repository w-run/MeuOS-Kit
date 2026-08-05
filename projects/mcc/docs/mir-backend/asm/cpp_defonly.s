.text
.globl B_seti
B_seti:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type B_seti, @function
.size B_seti, .-B_seti
.text
.globl main
main:
	endbr64
	movl $0, %eax
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
