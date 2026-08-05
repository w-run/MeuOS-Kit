.text
assign:
	endbr64
	subq $32, %rsp
	movl %edx, 16(%rsp)
	movq %rsi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl $0, %eax
.p2align 4
.Lbb2:
	cmpl %edx, %eax
	jge .Lbb4
	movslq %eax, %r8
	movl (%rsi, %r8, 4), %ecx
	addl $1, %ecx
	movl %ecx, (%rdi, %r8, 4)
	addl $1, %eax
	jmp .Lbb2
.Lbb4:
	addq $32, %rsp
	ret
.type assign, @function
.size assign, .-assign
.text
.globl funcy_type
funcy_type:
	endbr64
	ret
.type funcy_type, @function
.size funcy_type, .-funcy_type
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: restrict\000"
.data
.balign 1
.Lstring.5:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $1, -32(%rbp)
	movl $2, -28(%rbp)
	movl $3, -24(%rbp)
	movl $4, -20(%rbp)
	movl $4, %edx
	leaq -32(%rbp), %rsi
	leaq -16(%rbp), %rdi
	callq assign
	movl -16(%rbp), %eax
	cmpl $2, %eax
	jnz .Lbb12
	movl -12(%rbp), %eax
	cmpl $3, %eax
	jnz .Lbb12
	movl -8(%rbp), %eax
	cmpl $4, %eax
	jnz .Lbb12
	movl -4(%rbp), %eax
	cmpl $5, %eax
	jnz .Lbb12
	leaq .Lstring.5(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb13
.Lbb12:
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb13:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
