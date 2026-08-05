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
.globl Outer_Outer
Outer_Outer:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -16(%rbp)
	callq Inner_Inner@plt
	leave
	ret
.type Outer_Outer, @function
.size Outer_Outer, .-Outer_Outer
.text
.globl Outer_use
Outer_use:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -8(%rbp)
	callq Inner_get@plt
	movl %eax, -16(%rbp)
	leave
	ret
.type Outer_use, @function
.size Outer_use, .-Outer_use
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	leaq -48(%rbp), %rdi
	callq Outer_Outer@plt
	leaq -48(%rbp), %rdi
	callq Outer_use@plt
	movl %eax, -12(%rbp)
	cmpl $5, %eax
	jnz .Lbb14
	leaq -32(%rbp), %rdi
	callq Inner_Inner@plt
	movl $10, -32(%rbp)
	movl $5, %esi
	leaq -32(%rbp), %rdi
	callq Inner_addi@plt
	movl %eax, -16(%rbp)
	cmpl $15, %eax
	jnz .Lbb13
	movl $0, %eax
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
