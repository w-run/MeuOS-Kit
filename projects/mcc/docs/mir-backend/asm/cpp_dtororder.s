.text
.globl W_Wi
W_Wi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, 8(%rdi)
	movl $0, (%rdi)
	movl $0, 4(%rdi)
	addq $16, %rsp
	ret
.type W_Wi, @function
.size W_Wi, .-W_Wi
.text
.globl W_dtor
W_dtor:
	endbr64
	subq $32, %rsp
	movq %rdi, 0(%rsp)
	movq (%rdi), %rcx
	cmpq $0, %rcx
	jz .Lbb4
	movl (%rcx), %eax
	imull $10, %eax, %eax
	movl 8(%rdi), %edx
	addl %edx, %eax
	movl %eax, (%rcx)
.Lbb4:
	addq $32, %rsp
	ret
.type W_dtor, @function
.size W_dtor, .-W_dtor
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $64, %rsp
	movl $0, -64(%rbp)
	movl $1, %esi
	leaq -48(%rbp), %rdi
	callq W_Wi@plt
	movl $2, %esi
	leaq -32(%rbp), %rdi
	callq W_Wi@plt
	movl $3, %esi
	leaq -16(%rbp), %rdi
	callq W_Wi@plt
	leaq -64(%rbp), %rax
	movq %rax, -48(%rbp)
	leaq -64(%rbp), %rax
	movq %rax, -32(%rbp)
	leaq -64(%rbp), %rax
	movq %rax, -16(%rbp)
	leaq -16(%rbp), %rdi
	callq W_dtor@plt
	leaq -32(%rbp), %rdi
	callq W_dtor@plt
	leaq -48(%rbp), %rdi
	callq W_dtor@plt
	movl -64(%rbp), %eax
	subl $321, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
