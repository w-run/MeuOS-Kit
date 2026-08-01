.data
.balign 4
.globl ext1
ext1:
	.int 5
.data
.balign 8
.globl ext2
ext2:
	.quad ext1+0
.data
.balign 4
.globl ext3
ext3:
	.int 7
.text
.globl ext_fn1
ext_fn1:
	endbr64
	subq $16, %rsp
	movl %edi, %eax
	movl %eax, 0(%rsp)
	addq $16, %rsp
	ret
.type ext_fn1, @function
.size ext_fn1, .-ext_fn1
.text
.globl ext_fn2
ext_fn2:
	endbr64
	subq $16, %rsp
	movl %edi, %eax
	movl %eax, 0(%rsp)
	addq $16, %rsp
	ret
.type ext_fn2, @function
.size ext_fn2, .-ext_fn2
.section .note.GNU-stack,"",@progbits
