/* __tls_get_addr — General-Dynamic TLS runtime.
 *
 * Called by compiler-generated code when accessing a _Thread_local
 * variable in a shared library or with -fPIC.  The caller loads the
 * TLS GD descriptor (module ID + offset) and calls __tls_get_addr
 * to resolve the address for the current thread.
 *
 * This implementation is the WEAK, static path: it assumes a purely
 * static process (no daemon loader / dlopen), where the module ID is
 * always the main executable (module 1), so the address is just
 * `tp + ti_offset`.  In a dynamically-linked process ld.so exports a
 * STRONG, DTV-backed __tls_get_addr that wins over this one at run
 * time (standard dl behavior); that version resolves `DTV[ti_module]
 * + ti_offset` with per-thread lazy allocation.  Keeping this weak
 * symbol guarantees static-only builds still link and run.
 *
 * Architecture note:
 *   x86_64:       __tls_get_addr  — %fs:0       (Variant II TP)
 *   i386:         ___tls_get_addr — %gs:0       (Variant II TP, .S file)
 *   aarch64:      __tls_get_addr  — tpidr_el0    (Variant I  TP)
 *   riscv64:      __tls_get_addr  — tp          (Variant I  TP)
 *   loongarch64:  __tls_get_addr  — $tp/$r2     (Variant I  TP)
 *
 * Reference: ELF Handling For Thread-Local Storage (Ulrich Drepper).
 */

#include <stddef.h>
#include "../internal/tls.h"

/* x86_64 uses host cc (supports inline asm).  For i386 (compiled by mcc
 * which has no inline assembly support), the implementation lives in
 * src/arch/i386/__tls_get_addr.S and this file is not compiled.
 * aarch64/riscv64/loongarch64 mirror the x86_64 inline-asm form; when
 * built by mcc (no inline asm) a per-arch .S variant can be added later,
 * following the i386 precedent. */
#if defined(__x86_64__)

__attribute__((weak)) void *
__tls_get_addr(tls_index *ti)
{
	void *tp;
	__asm__("movq %%fs:0, %0" : "=r"(tp));
	return (char *)tp + ti->ti_offset;
}

#elif defined(__aarch64__)

__attribute__((weak)) void *
__tls_get_addr(tls_index *ti)
{
	void *tp;
	__asm__("mrs %0, tpidr_el0" : "=r"(tp));
	return (char *)tp + ti->ti_offset;
}

#elif defined(__riscv) && (__riscv_xlen == 64)

__attribute__((weak)) void *
__tls_get_addr(tls_index *ti)
{
	void *tp;
	__asm__("mv %0, tp" : "=r"(tp));
	return (char *)tp + ti->ti_offset;
}

#elif defined(__loongarch64)

__attribute__((weak)) void *
__tls_get_addr(tls_index *ti)
{
	void *tp;
	__asm__("move %0, $tp" : "=r"(tp));
	return (char *)tp + ti->ti_offset;
}

#else
/* For non-x86_64 archs the .S file provides the implementation. */
#endif
