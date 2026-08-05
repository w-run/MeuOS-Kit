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
