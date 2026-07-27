.arm
.text

/* void __meuos_restore_rt(void)
 * Signal-return trampoline.  Issue rt_sigreturn to unwind the signal frame.
 * ARM EABI syscall 173 = rt_sigreturn. */
.globl __meuos_restore_rt
.type __meuos_restore_rt,%function
__meuos_restore_rt:
	mov r7, #173
	svc #0
	/* rt_sigreturn does not return */
	bkpt #0

.section .note.GNU-stack,"",%progbits
