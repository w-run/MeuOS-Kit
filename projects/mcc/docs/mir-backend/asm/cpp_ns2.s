.data
.balign 4
.globl base
base:
	.int 10
.text
.globl add
add:
	endbr64
	subq $16, %rsp
	movl %esi, 4(%rsp)
	movl %edi, 0(%rsp)
	movl %edi, %eax
	addl %esi, %eax
	movq base@gotpcrel(%rip), %rcx
	movl (%rcx), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type add, @function
.size add, .-add
.text
.globl Counter_Counter
Counter_Counter:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Counter_Counter, @function
.size Counter_Counter, .-Counter_Counter
.text
.globl Counter_inc
Counter_inc:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl $1, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Counter_inc, @function
.size Counter_inc, .-Counter_inc
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $2, %esi
	movl $1, %edi
	callq add@plt
	movl %eax, -16(%rbp)
	cmpl $13, %eax
	jnz .Lbb10
	leaq -32(%rbp), %rdi
	callq Counter_Counter@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	leaq -32(%rbp), %rdi
	callq Counter_inc@plt
	movl -32(%rbp), %eax
	cmpl $2, %eax
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
