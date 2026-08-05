.text
.globl Worker_start
Worker_start:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -16(%rbp)
	movb $1, (%rdi)
	callq Worker_work@plt
	leave
	ret
.type Worker_start, @function
.size Worker_start, .-Worker_start
.text
.globl Worker_work
Worker_work:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl 4(%rdi), %eax
	addl $2, %eax
	movl %eax, 4(%rdi)
	addq $16, %rsp
	ret
.type Worker_work, @function
.size Worker_work, .-Worker_work
.text
.globl Worker_stop
Worker_stop:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movb $0, (%rdi)
	addq $16, %rsp
	ret
.type Worker_stop, @function
.size Worker_stop, .-Worker_stop
.text
.globl Worker_Worker
Worker_Worker:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movb $0, (%rdi)
	movl $0, 4(%rdi)
	addq $16, %rsp
	ret
.type Worker_Worker, @function
.size Worker_Worker, .-Worker_Worker
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	leaq -16(%rbp), %rdi
	callq Worker_Worker@plt
	leaq -16(%rbp), %rdi
	callq Worker_start@plt
	leaq -16(%rbp), %rdi
	callq Worker_start@plt
	movl -12(%rbp), %eax
	cmpl $4, %eax
	jnz .Lbb12
	leaq -16(%rbp), %rdi
	callq Worker_stop@plt
	movzbl -16(%rbp), %eax
	cmpl $0, %eax
	jnz .Lbb11
	movl $0, %eax
	jmp .Lbb13
.Lbb11:
	movl $2, %eax
	jmp .Lbb13
.Lbb12:
	movl $1, %eax
.Lbb13:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
