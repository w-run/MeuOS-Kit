.text
.globl add
add:
	endbr64
	subq $16, %rsp
	movl %esi, 4(%rsp)
	movl %edi, 0(%rsp)
	movl %edi, %eax
	addl %esi, %eax
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
	movl %eax, -16(%rbp)
	subl $3, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
