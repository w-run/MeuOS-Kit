.section .init
	ld.d $ra, $sp, 8
	ld.d $fp, $sp, 0
	addi.d $sp, $sp, 16
	jr $ra

.section .fini
	ld.d $ra, $sp, 8
	ld.d $fp, $sp, 0
	addi.d $sp, $sp, 16
	jr $ra

.section .note.GNU-stack,"",@progbits