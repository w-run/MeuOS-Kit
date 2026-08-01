.text
.globl A_A
A_A:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	addq $16, %rsp
	ret
.type A_A, @function
.size A_A, .-A_A
.text
.globl B_B
B_B:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $2, (%rdi)
	addq $16, %rsp
	ret
.type B_B, @function
.size B_B, .-B_B
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
	movl $3, 8(%rdi)
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
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq C_C@plt
	movl -32(%rbp), %eax
	cmpl $1, %eax
	jnz .Lbb14
	movl -28(%rbp), %eax
	cmpl $2, %eax
	jnz .Lbb13
	leaq -32(%rbp), %rdi
	callq C_sum@plt
	movl %eax, -16(%rbp)
	cmpl $6, %eax
	jnz .Lbb12
	movl $0, %eax
	jmp .Lbb15
.Lbb12:
	movl $3, %eax
	jmp .Lbb15
.Lbb13:
	movl $2, %eax
	jmp .Lbb15
.Lbb14:
	movl $1, %eax
.Lbb15:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
