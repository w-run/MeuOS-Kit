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
.globl Vec_operator_ploVec
Vec_operator_ploVec:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movq %rsi, -8(%rbp)
	movq %rdi, -16(%rbp)
	movq %rsi, -40(%rbp)
	movl (%rdi), %eax
	movl -40(%rbp), %ecx
	movl %eax, %esi
	addl %ecx, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Veci@plt
	movq -32(%rbp), %rax
	leave
	ret
.type Vec_operator_ploVec, @function
.size Vec_operator_ploVec, .-Vec_operator_ploVec
.text
.globl Vec_cmpoVec
Vec_cmpoVec:
	endbr64
	subq $32, %rsp
	movq %rsi, 16(%rsp)
	movq %rdi, 8(%rsp)
	movq %rsi, 0(%rsp)
	movl (%rdi), %eax
	imull $100, %eax, %eax
	movl 0(%rsp), %ecx
	addl %ecx, %eax
	addq $32, %rsp
	ret
.type Vec_cmpoVec, @function
.size Vec_cmpoVec, .-Vec_cmpoVec
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $160, %rsp
	movl $1, %esi
	leaq -128(%rbp), %rdi
	callq Vec_Veci@plt
	movl $2, %esi
	leaq -112(%rbp), %rdi
	callq Vec_Veci@plt
	movl $3, %esi
	leaq -96(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -80(%rbp), %rdi
	callq Vec_Veci@plt
	movq -112(%rbp), %rsi
	leaq -128(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -8(%rbp)
	movq %rax, -136(%rbp)
	movq -96(%rbp), %rsi
	leaq -136(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -16(%rbp)
	movq %rax, -144(%rbp)
	movq -80(%rbp), %rsi
	leaq -144(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -24(%rbp)
	movq %rax, -152(%rbp)
	movl -152(%rbp), %eax
	movl %eax, -64(%rbp)
	leaq -64(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -28(%rbp)
	cmpl $10, %eax
	jnz .Lbb10
	movl $1, %esi
	leaq -48(%rbp), %rdi
	callq Vec_Veci@plt
	movq -48(%rbp), %rsi
	leaq -128(%rbp), %rdi
	callq Vec_cmpoVec@plt
	movl %eax, -32(%rbp)
	jmp .Lbb11
.Lbb10:
	movl $1, %eax
.Lbb11:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
