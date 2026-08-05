.text
mk:
	endbr64
	subq $72, %rsp
	movq %rdi, %rax
	movl %ecx, 44(%rsp)
	movq %rdx, 48(%rsp)
	movl %esi, 40(%rsp)
	movq %rax, 32(%rsp)
	movl %esi, 0(%rsp)
	movl $0, 4(%rsp)
	movq %rdx, 8(%rsp)
	movzbl %cl, %ecx
	movsbl %cl, %ecx
	movb %cl, 16(%rsp)
	movb $0, 17(%rsp)
	movw $0, 18(%rsp)
	movl $0, 20(%rsp)
	movq 0(%rsp), %rcx
	movq %rcx, 0(%rax)
	movq 8(%rsp), %rcx
	movq %rcx, 8(%rax)
	movq 16(%rsp), %rcx
	movq %rcx, 16(%rax)
	addq $72, %rsp
	ret
.type mk, @function
.size mk, .-mk
.text
sum:
	endbr64
	movl 8(%rsp), %eax
	movslq %eax, %rax
	movq 16(%rsp), %rcx
	addq %rcx, %rax
	movzbl 24(%rsp), %ecx
	movsbq %cl, %rcx
	addq %rcx, %rax
	ret
.type sum, @function
.size sum, .-sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $96, %rsp
	movl $5, %ecx
	movl $4, %edx
	movl $3, %esi
	leaq -88(%rbp), %rdi
	callq mk
	movq %rax, -24(%rbp)
	movq (%rax), %rcx
	movq %rcx, -64(%rbp)
	movq 8(%rax), %rcx
	movq %rcx, -56(%rbp)
	movq 16(%rax), %rax
	movq %rax, -48(%rbp)
	subq $32, %rsp
	movq %rsp, %rcx
	movq -64(%rbp), %rax
	movq %rax, 0(%rcx)
	movq -56(%rbp), %rax
	movq %rax, 8(%rcx)
	movq -48(%rbp), %rax
	movq %rax, 16(%rcx)
	callq sum
	movq %rax, -32(%rbp)
	subq $-32, %rsp
	cmpq $12, %rax
	movl $1, %ecx
	movl $0, %eax
	cmovnz %ecx, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
