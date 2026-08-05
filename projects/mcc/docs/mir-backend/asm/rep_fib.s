.text
.globl fib
fib:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movl %edi, %ebx
	movl %ebx, -8(%rbp)
	cmpl $2, %ebx
	jl .Lbb2
	movl %ebx, %edi
	subl $1, %edi
	callq fib@plt
	xchgl %eax, %ebx
	movl %ebx, -12(%rbp)
	movl %eax, %edi
	subl $2, %edi
	callq fib@plt
	movl %eax, -16(%rbp)
	addl %ebx, %eax
	jmp .Lbb3
.Lbb2:
	movl %ebx, %eax
.Lbb3:
	popq %rbx
	leave
	ret
.type fib, @function
.size fib, .-fib
.data
.balign 1
.Lstring.3:
	.ascii "fib(10)=%d\012\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $10, %edi
	callq fib@plt
	movl %eax, %esi
	movl %esi, -12(%rbp)
	leaq .Lstring.3(%rip), %rdi
	callq printf@plt
	movl $10, %edi
	callq fib@plt
	movl %eax, -16(%rbp)
	cmpl $55, %eax
	movl $1, %ecx
	movl $0, %eax
	cmovnz %ecx, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
