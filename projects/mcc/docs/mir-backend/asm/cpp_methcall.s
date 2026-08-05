.text
.globl Bank_addi
Bank_addi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl %esi, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Bank_addi, @function
.size Bank_addi, .-Bank_addi
.text
.globl Bank_apply_bonus
Bank_apply_bonus:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movq %rdi, -16(%rbp)
	movl 4(%rdi), %esi
	callq Bank_addi@plt
	leave
	ret
.type Bank_apply_bonus, @function
.size Bank_apply_bonus, .-Bank_apply_bonus
.text
.globl Bank_init
Bank_init:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $5, 4(%rdi)
	addq $16, %rsp
	ret
.type Bank_init, @function
.size Bank_init, .-Bank_init
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	leaq -16(%rbp), %rdi
	callq Bank_init@plt
	leaq -16(%rbp), %rdi
	callq Bank_apply_bonus@plt
	movl -16(%rbp), %eax
	cmpl $5, %eax
	jnz .Lbb8
	movl $0, %eax
	jmp .Lbb9
.Lbb8:
	movl $1, %eax
.Lbb9:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
