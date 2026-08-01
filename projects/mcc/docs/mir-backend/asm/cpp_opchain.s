.text
.globl Money_Moneyi
Money_Moneyi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Money_Moneyi, @function
.size Money_Moneyi, .-Money_Moneyi
.text
.globl Money_get
Money_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Money_get, @function
.size Money_get, .-Money_get
.text
.globl Money_operator_ploMoney
Money_operator_ploMoney:
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
.type Money_operator_ploMoney, @function
.size Money_operator_ploMoney, .-Money_operator_ploMoney
.text
.globl Money_operator_mioMoney
Money_operator_mioMoney:
	endbr64
	subq $32, %rsp
	movq %rsi, 16(%rsp)
	movq %rdi, 8(%rsp)
	movq %rsi, 0(%rsp)
	movl (%rdi), %eax
	movl 0(%rsp), %ecx
	subl %ecx, %eax
	addq $32, %rsp
	ret
.type Money_operator_mioMoney, @function
.size Money_operator_mioMoney, .-Money_operator_mioMoney
.text
.globl Money_operator_mli
Money_operator_mli:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	imull %esi, %eax
	addq $16, %rsp
	ret
.type Money_operator_mli, @function
.size Money_operator_mli, .-Money_operator_mli
.text
.globl Money_operator_eqoMoney
Money_operator_eqoMoney:
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
.type Money_operator_eqoMoney, @function
.size Money_operator_eqoMoney, .-Money_operator_eqoMoney
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $96, %rsp
	movl $10, %esi
	leaq -96(%rbp), %rdi
	callq Money_Moneyi@plt
	movl $20, %esi
	leaq -80(%rbp), %rdi
	callq Money_Moneyi@plt
	movl $5, %esi
	leaq -64(%rbp), %rdi
	callq Money_Moneyi@plt
	movq -80(%rbp), %rsi
	leaq -96(%rbp), %rdi
	callq Money_operator_ploMoney@plt
	movl %eax, -16(%rbp)
	movl -64(%rbp), %ecx
	addl %ecx, %eax
	cmpl $35, %eax
	jnz .Lbb22
	movq -96(%rbp), %rsi
	leaq -80(%rbp), %rdi
	callq Money_operator_mioMoney@plt
	movl %eax, -20(%rbp)
	cmpl $10, %eax
	jnz .Lbb21
	movl $3, %esi
	leaq -96(%rbp), %rdi
	callq Money_operator_mli@plt
	movl %eax, -24(%rbp)
	cmpl $30, %eax
	jnz .Lbb20
	movq -80(%rbp), %rsi
	leaq -96(%rbp), %rdi
	callq Money_operator_eqoMoney@plt
	movl %eax, -28(%rbp)
	cmpl $0, %eax
	jnz .Lbb19
	movl $10, %esi
	leaq -48(%rbp), %rdi
	callq Money_Moneyi@plt
	movq -48(%rbp), %rsi
	leaq -96(%rbp), %rdi
	callq Money_operator_eqoMoney@plt
	movl %eax, -32(%rbp)
	cmpl $0, %eax
	jnz .Lbb18
	movl $5, %eax
	jmp .Lbb23
.Lbb18:
	movl $0, %eax
	jmp .Lbb23
.Lbb19:
	movl $4, %eax
	jmp .Lbb23
.Lbb20:
	movl $3, %eax
	jmp .Lbb23
.Lbb21:
	movl $2, %eax
	jmp .Lbb23
.Lbb22:
	movl $1, %eax
.Lbb23:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
