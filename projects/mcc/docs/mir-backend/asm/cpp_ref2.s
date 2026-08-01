.text
.globl Vec_Veci
Vec_Veci:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Vec_Veci, @function
.size Vec_Veci, .-Vec_Veci
.text
.globl Vec_get
Vec_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Vec_get, @function
.size Vec_get, .-Vec_get
.text
.globl Vec_bumpoVec
Vec_bumpoVec:
	endbr64
	subq $16, %rsp
	movq %rsi, 0(%rsp)
	movl (%rsi), %eax
	addl $10, %eax
	movl %eax, (%rsi)
	addq $16, %rsp
	ret
.type Vec_bumpoVec, @function
.size Vec_bumpoVec, .-Vec_bumpoVec
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $1, %esi
	leaq -48(%rbp), %rdi
	callq Vec_Veci@plt
	movl $2, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Veci@plt
	leaq -32(%rbp), %rsi
	leaq -48(%rbp), %rdi
	callq Vec_bumpoVec@plt
	leaq -32(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -12(%rbp)
	cmpl $12, %eax
	jnz .Lbb10
	leaq -48(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -16(%rbp)
	cmpl $1, %eax
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
