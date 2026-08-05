.text
.globl main
main:
	endbr64
	movl $0, %eax
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
