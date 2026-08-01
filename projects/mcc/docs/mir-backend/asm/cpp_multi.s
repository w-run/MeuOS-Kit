.text
.globl A_A
A_A:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $10, (%rdi)
	addq $16, %rsp
	ret
.type A_A, @function
.size A_A, .-A_A
.text
.globl A_getA
A_getA:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type A_getA, @function
.size A_getA, .-A_getA
.text
.globl B_B
B_B:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $20, (%rdi)
	addq $16, %rsp
	ret
.type B_B, @function
.size B_B, .-B_B
.text
.globl B_getB
B_getB:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type B_getB, @function
.size B_getB, .-B_getB
.text
.globl C_C
C_C:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movq %rdi, -16(%rbp)
	movq %rdi, %rbx
	callq A_A@plt
	movq %rbx, %rdi
	movq %rdi, %rbx
	addq $4, %rdi
	callq B_B@plt
	movq %rbx, %rdi
	movl $30, 8(%rdi)
	popq %rbx
	leave
	ret
.type C_C, @function
.size C_C, .-C_C
.text
.globl C_sum
C_sum:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	movl 8(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type C_sum, @function
.size C_sum, .-C_sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $56, %rsp
	pushq %rbx
	leaq -48(%rbp), %rdi
	callq C_C@plt
	movl -48(%rbp), %eax
	cmpl $10, %eax
	jnz .Lbb22
	leaq -48(%rbp), %rax
	movq %rax, %rdi
	addq $4, %rdi
	movl -44(%rbp), %eax
	cmpl $20, %eax
	jnz .Lbb21
	movq %rdi, %rbx
	leaq -48(%rbp), %rdi
	callq A_getA@plt
	movq %rbx, %rdi
	movl %eax, -24(%rbp)
	cmpl $10, %eax
	jnz .Lbb20
	callq B_getB@plt
	movl %eax, -28(%rbp)
	cmpl $20, %eax
	jnz .Lbb19
	leaq -48(%rbp), %rdi
	callq C_sum@plt
	movl %eax, -32(%rbp)
	cmpl $60, %eax
	jnz .Lbb18
	movl $0, %eax
	jmp .Lbb23
.Lbb18:
	movl $5, %eax
	jmp .Lbb23
.Lbb19:
	movl $4, %eax
	jmp .Lbb23
.Lbb20:
	movl $3, %eax
	jmp .Lbb23
.Lbb21:
	movl $2, %eax
	jmp .Lbb23
.Lbb22:
	movl $1, %eax
.Lbb23:
	popq %rbx
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
