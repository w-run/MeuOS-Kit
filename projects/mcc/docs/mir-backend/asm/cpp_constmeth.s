.text
.globl Bank_Bank
Bank_Bank:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $100, (%rdi)
	addq $16, %rsp
	ret
.type Bank_Bank, @function
.size Bank_Bank, .-Bank_Bank
.text
.globl Bank_depositi
Bank_depositi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl %esi, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Bank_depositi, @function
.size Bank_depositi, .-Bank_depositi
.text
.globl Bank_getK
Bank_getK:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Bank_getK, @function
.size Bank_getK, .-Bank_getK
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	leaq -48(%rbp), %rdi
	callq Bank_Bank@plt
	movl $50, %esi
	leaq -48(%rbp), %rdi
	callq Bank_depositi@plt
	leaq -48(%rbp), %rdi
	callq Bank_getK@plt
	movl %eax, -12(%rbp)
	cmpl $150, %eax
	jnz .Lbb10
	leaq -32(%rbp), %rdi
	callq Bank_Bank@plt
	leaq -32(%rbp), %rdi
	callq Bank_getK@plt
	movl %eax, -16(%rbp)
	cmpl $100, %eax
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
