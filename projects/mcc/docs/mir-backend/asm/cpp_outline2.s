.text
.globl Bank_Bank
Bank_Bank:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
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
.globl Bank_get
Bank_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Bank_get, @function
.size Bank_get, .-Bank_get
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Bank_Bank@plt
	movl $10, %esi
	leaq -32(%rbp), %rdi
	callq Bank_depositi@plt
	movl $20, %esi
	leaq -32(%rbp), %rdi
	callq Bank_depositi@plt
	leaq -32(%rbp), %rdi
	callq Bank_get@plt
	movl %eax, -16(%rbp)
	subl $30, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
