.text
.globl Counter_Counter
Counter_Counter:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Counter_Counter, @function
.size Counter_Counter, .-Counter_Counter
.text
.globl Counter_inc
Counter_inc:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl $1, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Counter_inc, @function
.size Counter_inc, .-Counter_inc
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Counter_Counter@plt
	leaq -16(%rbp), %rdi
	callq Counter_Counter@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	leaq -16(%rbp), %rdi
	callq Counter_inc@plt
	movl -32(%rbp), %eax
	cmpl $2, %eax
	jnz .Lbb8
	movl -16(%rbp), %eax
	cmpl $1, %eax
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
