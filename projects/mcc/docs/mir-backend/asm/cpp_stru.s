.text
.globl Vec_zero
Vec_zero:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Vec_zero, @function
.size Vec_zero, .-Vec_zero
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	leaq -16(%rbp), %rdi
	callq Vec_zero@plt
	movl -16(%rbp), %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
