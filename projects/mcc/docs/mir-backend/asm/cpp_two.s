.text
.globl W_a
W_a:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -16(%rbp)
	callq W_b@plt
	leave
	ret
.type W_a, @function
.size W_a, .-W_a
.text
.globl W_b
W_b:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	addq $16, %rsp
	ret
.type W_b, @function
.size W_b, .-W_b
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	leaq -16(%rbp), %rdi
	callq W_a@plt
	movl -16(%rbp), %eax
	subl $1, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
