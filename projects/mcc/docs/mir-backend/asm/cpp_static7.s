.text
.globl R_read
R_read:
	endbr64
	movq R_count@gotpcrel(%rip), %rax
	movl (%rax), %eax
	ret
.type R_read, @function
.size R_read, .-R_read
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
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq R_read@plt
	movl %eax, -16(%rbp)
	subl $7, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
