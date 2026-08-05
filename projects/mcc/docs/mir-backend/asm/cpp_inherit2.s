.text
.globl Shape_Shape
Shape_Shape:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Shape_Shape, @function
.size Shape_Shape, .-Shape_Shape
.text
.globl Shape_set_areai
Shape_set_areai:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Shape_set_areai, @function
.size Shape_set_areai, .-Shape_set_areai
.text
.globl Square_Square
Square_Square:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movq %rdi, -16(%rbp)
	movq %rdi, %rbx
	callq Shape_Shape@plt
	movq %rbx, %rdi
	movl $0, 4(%rdi)
	popq %rbx
	leave
	ret
.type Square_Square, @function
.size Square_Square, .-Square_Square
.text
.globl Square_compute
Square_compute:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl 4(%rdi), %eax
	imull %eax, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Square_compute, @function
.size Square_compute, .-Square_compute
.text
.globl Square_area
Square_area:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Square_area, @function
.size Square_area, .-Square_area
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Square_Square@plt
	movl $6, -28(%rbp)
	leaq -32(%rbp), %rdi
	callq Square_compute@plt
	leaq -32(%rbp), %rdi
	callq Square_area@plt
	movl %eax, -12(%rbp)
	cmpl $36, %eax
	jnz .Lbb14
	movl $100, %esi
	leaq -32(%rbp), %rdi
	callq Shape_set_areai@plt
	leaq -32(%rbp), %rdi
	callq Square_area@plt
	movl %eax, -16(%rbp)
	cmpl $100, %eax
	jnz .Lbb13
	movl $0, %eax
	jmp .Lbb15
.Lbb13:
	movl $2, %eax
	jmp .Lbb15
.Lbb14:
	movl $1, %eax
.Lbb15:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
