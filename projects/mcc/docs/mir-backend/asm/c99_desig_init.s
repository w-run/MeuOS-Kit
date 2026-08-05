.data
.balign 1
.Lstring.2:
	.ascii "FAIL: .field init\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: [index] init\000"
.data
.balign 1
.Lstring.4:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $11, -32(%rbp)
	movl $0, -28(%rbp)
	movl $99, -24(%rbp)
	movl $0, -20(%rbp)
	movl $0, -16(%rbp)
	movl -32(%rbp), %eax
	cmpl $11, %eax
	jnz .Lbb4
	movl -28(%rbp), %eax
	cmpl $0, %eax
	jnz .Lbb4
	movl -24(%rbp), %eax
	cmpl $99, %eax
	jnz .Lbb4
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb5
.Lbb4:
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb5:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
