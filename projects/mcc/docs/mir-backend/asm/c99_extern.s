.data
.balign 1
.Lstring.2:
	.ascii "FAIL: ext1\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: *ext2\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: ext3\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: ext_fn1\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL: ext_fn2\000"
.data
.balign 1
.Lstring.7:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq ext1@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $5, %eax
	jnz .Lbb10
	movq ext2@gotpcrel(%rip), %rax
	movq (%rax), %rax
	movl (%rax), %eax
	cmpl $5, %eax
	jnz .Lbb9
	movq ext3@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $7, %eax
	jnz .Lbb8
	movl $5, %edi
	callq ext_fn1@plt
	movl %eax, -12(%rbp)
	cmpl $5, %eax
	jnz .Lbb7
	movl $8, %edi
	callq ext_fn2@plt
	movl %eax, -16(%rbp)
	cmpl $8, %eax
	jnz .Lbb6
	leaq .Lstring.7(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb11
.Lbb6:
	leaq .Lstring.6(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb11
.Lbb7:
	leaq .Lstring.5(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb11
.Lbb8:
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb11
.Lbb9:
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb11
.Lbb10:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb11:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
