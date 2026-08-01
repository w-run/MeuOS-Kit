.text
.globl Counter_inc
Counter_inc:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Counter_inc, @function
.size Counter_inc, .-Counter_inc
.text
.globl Counter_reset
Counter_reset:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $1, 4(%rdi)
	addq $16, %rsp
	ret
.type Counter_reset, @function
.size Counter_reset, .-Counter_reset
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
	callq Counter_reset@plt
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
