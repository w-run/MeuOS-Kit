.text
.globl Watcher_Watcher
Watcher_Watcher:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $0, 4(%rdi)
	addq $16, %rsp
	ret
.type Watcher_Watcher, @function
.size Watcher_Watcher, .-Watcher_Watcher
.text
.globl Watcher_dtor
Watcher_dtor:
	endbr64
	subq $32, %rsp
	movq %rdi, 0(%rsp)
	movq (%rdi), %rcx
	cmpq $0, %rcx
	jz .Lbb4
	movl (%rcx), %eax
	addl $1, %eax
	movl %eax, (%rcx)
.Lbb4:
	addq $32, %rsp
	ret
.type Watcher_dtor, @function
.size Watcher_dtor, .-Watcher_dtor
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	movl $0, -32(%rbp)
	leaq -16(%rbp), %rdi
	callq Watcher_Watcher@plt
	leaq -32(%rbp), %rax
	movq %rax, -16(%rbp)
	movl -32(%rbp), %eax
	cmpl $0, %eax
	jnz .Lbb7
	leaq -16(%rbp), %rdi
	callq Watcher_dtor@plt
	movl $1, %eax
	jmp .Lbb8
.Lbb7:
	movl $9, %eax
.Lbb8:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
