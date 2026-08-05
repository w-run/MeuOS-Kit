#include <features.h>
#include <stdio.h>

/* Validates the opt-in glibc-compat surface (built with
 * -D__MEUOS_GLIBC_COMPAT__, as injected by meuos-glibc-compat.pc).
 * Without that flag (the default for unaugmented builds) __GLIBC__ must be
 * absent and this test is not compiled with it. */

int
main(void)
{
#if !defined(__GLIBC__)
	puts("FAIL: __GLIBC__ not exposed under __MEUOS_GLIBC_COMPAT__");
	return 1;
#endif
	if (__GLIBC__ != 2 || __GLIBC_MINOR__ != 34) {
		printf("FAIL: __GLIBC__=%d __GLIBC_MINOR__=%d (expect 2.34)\n",
		       __GLIBC__, __GLIBC_MINOR__);
		return 2;
	}
	/* __GLIBC_PREREQ must gate version-conditional code. */
#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 34)
	puts("FAIL: __GLIBC_PREREQ(2,34) not satisfied");
	return 3;
#endif
	/* And the compatible GNU view must be on. */
#if !defined(__USE_GNU)
	puts("FAIL: __USE_GNU not enabled under compat");
	return 4;
#endif
	puts("PASS glibc-compat");
	return 0;
}
