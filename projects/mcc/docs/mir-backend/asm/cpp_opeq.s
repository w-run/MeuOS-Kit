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
.globl Vec_operator_eqoVec
Vec_operator_eqoVec:
	endbr64
	subq $32, %rsp
	movq %rsi, 16(%rsp)
	movq %rdi, 8(%rsp)
	movq %rsi, 0(%rsp)
	movl (%rdi), %ecx
	movl 0(%rsp), %eax
	cmpl %eax, %ecx
	setz %al
	movzbl %al, %eax
	addq $32, %rsp
	ret
.type Vec_operator_eqoVec, @function
.size Vec_operator_eqoVec, .-Vec_operator_eqoVec
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
	movl $1, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Veci@plt
	movq -32(%rbp), %rsi
	leaq -48(%rbp), %rdi
	callq Vec_operator_eqoVec@plt
	movl %eax, -16(%rbp)
	cmpl $0, %eax
	jnz .Lbb6
	movl $1, %eax
	jmp .Lbb7
.Lbb6:
	movl $0, %eax
.Lbb7:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
