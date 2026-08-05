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
.globl Counter_get
Counter_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Counter_get, @function
.size Counter_get, .-Counter_get
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Counter_Counter@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	leaq -32(%rbp), %rdi
	callq Counter_get@plt
	movl %eax, -16(%rbp)
	subl $2, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
