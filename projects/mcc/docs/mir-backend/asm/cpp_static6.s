.text
.globl R_getS
R_getS:
	endbr64
	movq R_count@gotpcrel(%rip), %rax
	movl (%rax), %eax
	ret
.type R_getS, @function
.size R_getS, .-R_getS
.data
.balign 4
.globl R_count
R_count:
	.int 7
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	callq R_getS@plt
	movl %eax, -16(%rbp)
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
