.text
.globl Foo_seti
Foo_seti:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Foo_seti, @function
.size Foo_seti, .-Foo_seti
.text
.globl helper
helper:
	endbr64
	subq $16, %rsp
	movl %esi, 4(%rsp)
	movl %edi, 0(%rsp)
	movl %edi, %eax
	addl %esi, %eax
	addq $16, %rsp
	ret
.type helper, @function
.size helper, .-helper
