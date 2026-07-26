.arm
.text

/* void __meuos_set_tls(void *ptr)
 * Set the ARM thread-pointer register (cp15 c13) to ptr.
 * Used by crt1.o to set the main thread TP before calling main. */
.globl __meuos_set_tls
.type __meuos_set_tls,%function
__meuos_set_tls:
	mcr p15, 0, r0, c13, c0, 3
	bx lr

.section .note.GNU-stack,"",%progbits
