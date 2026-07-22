/* i386 ___tls_get_addr — General-Dynamic TLS runtime.
 *
 * Called by compiler-generated code for general-dynamic TLS access.
 * The caller leaves a tls_index* on the stack (cdecl).
 * Returns the address of the TLS variable for the current thread.
 *
 * i386 cdecl: argument on stack at [esp+4]; return in %eax.
 * Variant II TLS: %gs:0 is the thread pointer (self-pointer).
 */

.globl ___tls_get_addr
.type ___tls_get_addr,@function
___tls_get_addr:
	movl	4(%esp), %eax		/* tls_index *ti */
	movl	4(%eax), %eax		/* ti->ti_offset */
	addl	%gs:0, %eax		/* + thread pointer */
	ret

.section .note.GNU-stack,"",@progbits
