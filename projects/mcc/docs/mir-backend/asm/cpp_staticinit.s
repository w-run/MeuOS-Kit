.data
.balign 4
.globl Config_def
Config_def:
	.int 99
.data
.balign 4
.globl Config_limit
Config_limit:
	.int 100
.text
.globl main
main:
	endbr64
	movq Config_limit@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $100, %eax
	jnz .Lbb4
	movq Config_def@gotpcrel(%rip), %rax
	movl (%rax), %eax
	cmpl $99, %eax
	jnz .Lbb3
	movl $0, %eax
	jmp .Lbb5
.Lbb3:
	movl $2, %eax
	jmp .Lbb5
.Lbb4:
	movl $1, %eax
.Lbb5:
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
