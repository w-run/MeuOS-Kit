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
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $80, %rsp
	movl $3, %esi
	leaq -64(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -48(%rbp), %rdi
	callq Vec_Veci@plt
	movq -48(%rbp), %rsi
	leaq -64(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -8(%rbp)
	movq %rax, -72(%rbp)
	movl -72(%rbp), %eax
	movl %eax, -32(%rbp)
	leaq -32(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -16(%rbp)
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
