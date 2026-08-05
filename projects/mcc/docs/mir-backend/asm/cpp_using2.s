.data
.balign 4
.globl x
x:
	.int 5
.data
.balign 4
.globl y
y:
	.int 6
.text
.globl main
main:
	endbr64
	movq x@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $5, %eax
	jnz .Lbb2
	movl $0, %eax
	jmp .Lbb3
.Lbb2:
	movl $1, %eax
.Lbb3:
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
