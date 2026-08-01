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
.globl Counter_dtor
Counter_dtor:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $4294967295, (%rdi)
	addq $16, %rsp
	ret
.type Counter_dtor, @function
.size Counter_dtor, .-Counter_dtor
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $40, %rsp
	pushq %rbx
	leaq -32(%rbp), %rdi
	callq Counter_Counter@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	movl -32(%rbp), %eax
	cmpl $1, %eax
	movl $1, %ecx
	movl $0, %eax
	movl %eax, %ebx
	cmovnz %ecx, %ebx
	leaq -32(%rbp), %rdi
	callq Counter_dtor@plt
	movl %ebx, %eax
	popq %rbx
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
