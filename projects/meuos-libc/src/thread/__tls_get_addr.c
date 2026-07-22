/* __tls_get_addr — General-Dynamic TLS runtime for x86_64/i386.
 *
 * Called by compiler-generated code when accessing a _Thread_local
 * variable in a shared library or with -fPIC.  The caller loads the
 * TLS GD descriptor (module ID + offset) and calls __tls_get_addr
 * to resolve the address for the current thread.
 *
 * In a purely static environment (no dlopen), the module ID is always
 * the main executable (module 1).  The function simply adds the offset
 * to the thread pointer.
 *
 * Architecture note:
 *   x86_64:  __tls_get_addr  (2 underscores) — %fs:0 for Variant II TP
 *   i386:    ___tls_get_addr (3 underscores) — %gs:0 for Variant II TP
 *
 * Reference: ELF Handling For Thread-Local Storage (Ulrich Drepper),
 * Section 5.3.2 "General Dynamic Access".
 */

#include <stddef.h>

typedef struct {
	unsigned long ti_module;
	unsigned long ti_offset;
} tls_index;

/* x86_64 uses host cc (supports inline asm).  For i386 (compiled by mcc
 * which has no inline assembly support), the implementation lives in
 * src/arch/i386/__tls_get_addr.S and this file is not compiled. */
#if defined(__x86_64__)

void *__tls_get_addr(tls_index *ti)
{
	void *tp;
	__asm__("movq %%fs:0, %0" : "=r"(tp));
	return (char *)tp + ti->ti_offset;
}

#else
/* For non-x86_64 archs the .S file provides the implementation. */
#endif
