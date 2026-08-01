.text
.globl Foo_seti
Foo_seti:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Foo_seti, @function
.size Foo_seti, .-Foo_seti
.text
.globl helper
helper:
	endbr64
	subq $16, %rsp
	movl %esi, 4(%rsp)
	movl %edi, 0(%rsp)
	movl %edi, %eax
	addl %esi, %eax
	addq $16, %rsp
	ret
.type helper, @function
.size helper, .-helper
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $0, -32(%rbp)
	movl $1, %esi
	leaq -32(%rbp), %rdi
	callq Foo_seti@plt
	movl $2, %esi
	movl $40, %edi
	callq helper@plt
	movl %eax, -16(%rbp)
	cmpl $42, %eax
	jnz .Lbb8
	movl -32(%rbp), %eax
	cmpl $1, %eax
	jnz .Lbb7
	movl $0, %eax
	jmp .Lbb9
.Lbb7:
	movl $2, %eax
	jmp .Lbb9
.Lbb8:
	movl $1, %eax
.Lbb9:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
