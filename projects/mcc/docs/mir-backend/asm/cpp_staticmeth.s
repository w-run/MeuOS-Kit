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
.globl Counter_resetS
Counter_resetS:
	endbr64
	movq Counter_total@gotpcrel(%rip), %rax
	movl $0, (%rax)
	ret
.type Counter_resetS, @function
.size Counter_resetS, .-Counter_resetS
.text
.globl Counter_addiS
Counter_addiS:
	endbr64
	subq $16, %rsp
	movl %edi, 0(%rsp)
	movq Counter_total@gotpcrel(%rip), %rax
	movl (%rax), %eax
	addl %edi, %eax
	movq Counter_total@gotpcrel(%rip), %rcx
	movl %eax, (%rcx)
	addq $16, %rsp
	ret
.type Counter_addiS, @function
.size Counter_addiS, .-Counter_addiS
.data
.balign 4
.globl Counter_total
Counter_total:
	.int 100
