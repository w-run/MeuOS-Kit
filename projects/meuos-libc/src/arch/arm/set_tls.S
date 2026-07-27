.text

/* void __meuos_set_tls(void *ptr)
 * Set the ARM thread-pointer register (TPIDRURO) to ptr.
 *
 * Direct access to `mcr p15, 0, r0, c13, c0, 3` is privileged (kernel
 * mode only).  In user-space, set TPIDRURO via the kernel's __kuser_set_tls
 * helper at the fixed address 0xffff0fe0 (available in every process). */
.globl __meuos_set_tls
.type __meuos_set_tls,%function
__meuos_set_tls:
	ldr r1, =0xffff0fe0
	bx r1

.section .note.GNU-stack,"",%progbits
