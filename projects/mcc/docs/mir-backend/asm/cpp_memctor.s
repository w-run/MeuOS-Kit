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
.globl Outer_Outer
Outer_Outer:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movq %rdi, -16(%rbp)
	movq %rdi, %rbx
	callq Inner_Inner@plt
	movq %rbx, %rdi
	movl $1, 4(%rdi)
	popq %rbx
	leave
	ret
.type Outer_Outer, @function
.size Outer_Outer, .-Outer_Outer
.text
.globl Outer_use
Outer_use:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type Outer_use, @function
.size Outer_use, .-Outer_use
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Outer_Outer@plt
	leaq -32(%rbp), %rdi
	callq Outer_use@plt
	movl %eax, -16(%rbp)
	cmpl $6, %eax
	jnz .Lbb10
	movl -32(%rbp), %eax
	cmpl $5, %eax
	jnz .Lbb9
	movl $0, %eax
	jmp .Lbb11
.Lbb9:
	movl $2, %eax
	jmp .Lbb11
.Lbb10:
	movl $1, %eax
.Lbb11:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
