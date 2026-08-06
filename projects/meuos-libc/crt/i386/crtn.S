.section .init
	popl %ebp
	ret

.section .fini
	popl %ebp
	ret

.section .note.GNU-stack,"",@progbits