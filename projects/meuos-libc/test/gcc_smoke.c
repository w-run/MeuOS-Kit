/* gcc/clang end-to-end smoke test against the MeuOS libc sysroot.
 * Exercises: MeuOS headers, libc-meuos.a (malloc/printf/strings), a global
 * array (BSS/.data), 64-bit division (native div), and an explicit
 * __divdi3 call to prove libgcc-meuos.a resolves the soft helper. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern long long __divdi3(long long, long long);

static long global_array[16];           /* .bss */

static const char *label = "meuos-smoke";   /* .rodata/.data */

int
main(int argc, char **argv)
{
	char *p;
	long long q;
	int i;

	(void)argc;
	(void)argv;

	/* string + stdio + a global array init/reset */
	if (strcmp(label, "meuos-smoke") != 0)
		return 1;
	for (i = 0; i < 16; ++i)
		global_array[i] = i * i;

	/* malloc/free */
	p = malloc(128);
	if (!p)
		return 2;
	memcpy(p, "two mushrooms", 13);
	p[13] = '\0';
	if (strcmp(p, "two mushrooms") != 0)
		return 3;
	free(p);

	/* native 64-bit division (gcc emits divq, no helper) */
	q = -123456789LL / 1000LL;
	if (q != -123456LL)
		return 4;

	/* explicit soft-helper reference through libgcc-meuos.a */
	if (__divdi3(-100LL, 7LL) != -14LL)
		return 5;

	printf("gcc-meuos-smoke ok: %d %lld %s\n", global_array[15] == 225, q, label);
	return 0;
}
