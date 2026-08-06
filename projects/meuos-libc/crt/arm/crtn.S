.arm
.section .init
	pop {r11, pc}

.section .fini
	pop {r11, pc}

.section .note.GNU-stack,"",%progbits