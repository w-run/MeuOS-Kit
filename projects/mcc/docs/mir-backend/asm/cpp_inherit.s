.text
.globl Base_Base
Base_Base:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $10, (%rdi)
	addq $16, %rsp
	ret
.type Base_Base, @function
.size Base_Base, .-Base_Base
.text
.globl Base_bump
Base_bump:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl $1, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Base_bump, @function
.size Base_bump, .-Base_bump
.text
.globl Derived_Derived
Derived_Derived:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movq %rdi, -16(%rbp)
	movq %rdi, %rbx
	callq Base_Base@plt
	movq %rbx, %rdi
	movl $100, 4(%rdi)
	popq %rbx
	leave
	ret
.type Derived_Derived, @function
.size Derived_Derived, .-Derived_Derived
.text
.globl Derived_total
Derived_total:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type Derived_total, @function
.size Derived_total, .-Derived_total
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Derived_Derived@plt
	leaq -32(%rbp), %rdi
	callq Base_bump@plt
	leaq -32(%rbp), %rdi
	callq Derived_total@plt
	movl %eax, -16(%rbp)
	movl -32(%rbp), %ecx
	cmpl $11, %ecx
	jnz .Lbb12
	cmpl $111, %eax
	jnz .Lbb11
	movl $0, %eax
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
