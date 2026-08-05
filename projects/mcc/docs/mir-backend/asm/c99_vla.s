.data
.balign 1
.Lstring.2:
	.ascii "FAIL\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: sizeof VLA\000"
.data
.balign 1
.Lstring.4:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $0, %ecx
	movl $0, %eax
.p2align 4
.Lbb2:
	cmpl $5, %ecx
	jge .Lbb4
	movslq %ecx, %rdx
	movl %ecx, -48(%rbp, %rdx, 4)
	movl -48(%rbp, %rdx, 4), %edx
	addl %edx, %eax
	addl $1, %ecx
	jmp .Lbb2
.Lbb4:
	cmpl $10, %eax
	jnz .Lbb6
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb7
.Lbb6:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb7:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
