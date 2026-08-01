.text
.globl Inner_Inner
Inner_Inner:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $5, (%rdi)
	addq $16, %rsp
	ret
.type Inner_Inner, @function
.size Inner_Inner, .-Inner_Inner
.text
.globl Inner_get
Inner_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Inner_get, @function
.size Inner_get, .-Inner_get
.text
.globl Inner_addi
Inner_addi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl %esi, %eax
	addq $16, %rsp
	ret
.type Inner_addi, @function
.size Inner_addi, .-Inner_addi
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	leaq -48(%rbp), %rdi
	callq Inner_Inner@plt
	leaq -48(%rbp), %rdi
	callq Inner_get@plt
	movl %eax, -8(%rbp)
	cmpl $5, %eax
	jnz .Lbb12
	movl $7, %esi
	leaq -48(%rbp), %rdi
	callq Inner_addi@plt
	movl %eax, -12(%rbp)
	cmpl $12, %eax
	jnz .Lbb11
	leaq -32(%rbp), %rdi
	callq Inner_Inner@plt
	movl $100, -32(%rbp)
	leaq -32(%rbp), %rdi
	callq Inner_get@plt
	movl %eax, -16(%rbp)
	cmpl $100, %eax
	jnz .Lbb10
	movl $0, %eax
	jmp .Lbb13
.Lbb10:
	movl $3, %eax
	jmp .Lbb13
.Lbb11:
	movl $2, %eax
	jmp .Lbb13
.Lbb12:
	movl $1, %eax
.Lbb13:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
