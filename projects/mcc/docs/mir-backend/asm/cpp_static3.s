.text
.globl Math_twiceiS
Math_twiceiS:
	endbr64
	subq $16, %rsp
	movl %edi, 0(%rsp)
	imull $2, %edi, %eax
	addq $16, %rsp
	ret
.type Math_twiceiS, @function
.size Math_twiceiS, .-Math_twiceiS
.text
.globl Math_squareiS
Math_squareiS:
	endbr64
	subq $16, %rsp
	movl %edi, 0(%rsp)
	movl %edi, %eax
	imull %edi, %eax
	addq $16, %rsp
	ret
.type Math_squareiS, @function
.size Math_squareiS, .-Math_squareiS
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movl $5, %edi
	callq Math_twiceiS@plt
	movl %eax, %ebx
	movl %ebx, -12(%rbp)
	movl $3, %edi
	callq Math_squareiS@plt
	movl %eax, -16(%rbp)
	addl %ebx, %eax
	subl $19, %eax
	popq %rbx
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
