.data
.balign 1
.Lstring.2:
	.ascii "FAIL\000"
.data
.balign 1
.Lstring.3:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $1, -16(%rbp)
	movl $2, -12(%rbp)
	movl $3, -8(%rbp)
	movl -8(%rbp), %eax
	cmpl $3, %eax
	jnz .Lbb2
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb3
.Lbb2:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb3:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
