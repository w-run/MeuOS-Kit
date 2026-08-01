.text
.globl Guard_hide
Guard_hide:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Guard_hide, @function
.size Guard_hide, .-Guard_hide
.text
.globl Guard_Guard
Guard_Guard:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	addq $16, %rsp
	ret
.type Guard_Guard, @function
.size Guard_Guard, .-Guard_Guard
.text
.globl Guard_get
Guard_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Guard_get, @function
.size Guard_get, .-Guard_get
