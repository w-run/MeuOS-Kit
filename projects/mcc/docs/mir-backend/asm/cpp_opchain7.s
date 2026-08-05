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
	subq $176, %rsp
	movl $1, %esi
	leaq -144(%rbp), %rdi
	callq Vec_Veci@plt
	movl $2, %esi
	leaq -128(%rbp), %rdi
	callq Vec_Veci@plt
	movl $3, %esi
	leaq -112(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -96(%rbp), %rdi
	callq Vec_Veci@plt
	movq -128(%rbp), %rsi
	leaq -144(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -16(%rbp)
	movq %rax, -152(%rbp)
	movq -112(%rbp), %rsi
	leaq -152(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -24(%rbp)
	movq %rax, -160(%rbp)
	movq -96(%rbp), %rsi
	leaq -160(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -32(%rbp)
	movq %rax, -168(%rbp)
	movl -168(%rbp), %eax
	movl %eax, -80(%rbp)
	leaq -80(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -40(%rbp)
	cmpl $10, %eax
	jnz .Lbb14
	leaq -144(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -44(%rbp)
	cmpl $1, %eax
	jnz .Lbb13
	movl $1, %esi
	leaq -64(%rbp), %rdi
	callq Vec_Veci@plt
	movq -64(%rbp), %rsi
	leaq -144(%rbp), %rdi
	callq Vec_operator_eqoVec@plt
	movl %eax, -48(%rbp)
	cmpl $0, %eax
	jnz .Lbb12
	movl $3, %eax
	jmp .Lbb15
.Lbb12:
	movl $0, %eax
	jmp .Lbb15
.Lbb13:
	movl $2, %eax
	jmp .Lbb15
.Lbb14:
	movl $1, %eax
.Lbb15:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
