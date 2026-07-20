.text

/* void __meuos_load_gs(unsigned short selector)
 * Load the %gs segment register with the given GDT/LDT selector. */
.globl __meuos_load_gs
.type __meuos_load_gs,@function
__meuos_load_gs:
	movl 4(%esp), %eax
	movw %ax, %gs
	ret

.section .note.GNU-stack,"",@progbits
