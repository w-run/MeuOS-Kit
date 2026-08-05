.data
.balign 4
.globl val
val:
	.int 5
.text
.globl twice
twice:
	endbr64
	subq $16, %rsp
	movl %edi, 0(%rsp)
	imull $2, %edi, %eax
	addq $16, %rsp
	ret
.type twice, @function
.size twice, .-twice
.text
.globl add
add:
	endbr64
	subq $16, %rsp
	movl %esi, 4(%rsp)
	movl %edi, 0(%rsp)
	movl %edi, %eax
	addl %esi, %eax
	movq val@gotpcrel(%rip), %rcx
	movl (%rcx), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type add, @function
.size add, .-add
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $2, %esi
	movl $1, %edi
	callq add@plt
	movl %eax, -12(%rbp)
	cmpl $8, %eax
	jnz .Lbb10
	movl $4, %edi
	callq twice@plt
	movl %eax, -16(%rbp)
	cmpl $8, %eax
	jnz .Lbb9
	movq val@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $5, %eax
	jnz .Lbb8
	movl $0, %eax
	jmp .Lbb11
.Lbb8:
	movl $3, %eax
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
