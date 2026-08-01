.data
.balign 4
.globl R_count
R_count:
	.int 42
.text
.globl main
main:
	endbr64
	movq R_count@gotpcrel(%rip), %rax
	movl (%rax), %eax
	subl $42, %eax
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
