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
	movl $0, %eax
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
