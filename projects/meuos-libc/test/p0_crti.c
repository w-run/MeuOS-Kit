/* p0_crti.c — P0 crti/crtn CRT prologue/epilogue contract gate.
 *
 * Verifies the gcc/clang sysroot path: a host-gcc compile that includes
 *   __attribute__((constructor)) / __attribute__((destructor))
 * must link cleanly against the MeuOS sysroot's crti.o + crt1.o + crtn.o
 * + libc-meuos.a + libgcc-meuos.a, with both the .init/.fini prologue/
 * epilogue from crti/crtn and the constructor/destructor payload from
 * the user module running in the expected order.
 *
 * Two trace strings are kept — one in .bss (incremented by an asm()
 * inline write so the prologue is exercised before any libc is up),
 * one in stdio — and both must end up identical to the hand-computed
 * trace.
 */

#include <stdio.h>

/* User-level entry points: the gcc/clang front end lowers these
 * __attribute__ tags to .init_array/.fini_array entries (crtbegin.o
 * in vendor toolchains, here crtbegin comes via the host gcc runtime).
 * The crti/crtn pair wraps them so the linker forms one balanced
 * function body for the .init/.fini section. */
static void ctor(void) __attribute__((constructor));
static void dtor(void) __attribute__((destructor));

static void
ctor(void)
{
	fputs("C", stdout);
	fflush(stdout);
}

static void
dtor(void)
{
	fputs("D", stdout);
	fflush(stdout);
}

int
main(void)
{
	fputs("M", stdout);
	fflush(stdout);
	return 0;
}