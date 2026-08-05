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
.globl Vec_operator_ploVec
Vec_operator_ploVec:
	endbr64
	subq $32, %rsp
	movq %rsi, 16(%rsp)
	movq %rdi, 8(%rsp)
	movq %rsi, 0(%rsp)
	movl (%rdi), %eax
	movl 0(%rsp), %ecx
	addl %ecx, %eax
	addq $32, %rsp
	ret
.type Vec_operator_ploVec, @function
.size Vec_operator_ploVec, .-Vec_operator_ploVec
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $3, %esi
	leaq -48(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Veci@plt
	movq -32(%rbp), %rsi
	leaq -48(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movl %eax, -16(%rbp)
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
