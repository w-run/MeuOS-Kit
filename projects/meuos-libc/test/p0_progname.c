/* p0_progname.c — P0 §2.2/§2.3 entry contract gate.
 *
 * Verifies:
 *   - __progname / __progname_full (from crt1 startup)
 *   - program_invocation_name / program_invocation_short_name (from <errno.h>)
 *   - getauxval(AT_RANDOM) returns a non-zero address
 *   - getauxval(AT_PAGESZ) returns a sensible page size
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>

extern const char *__progname;
extern const char *__progname_full;

/* basename helper (same logic as startup.c's base_name). */
static const char *
basename_only(const char *s)
{
	const char *base = s;
	if (!s) return NULL;
	for (; *s; ++s) {
		if (*s == '/')
			base = s + 1;
	}
	return base;
}

int
main(int argc, char **argv)
{
	unsigned long pagesz, random;

	/* argv[0] must be present. */
	if (argc < 1 || !argv[0]) {
		puts("FAIL: no argv[0]");
		return 1;
	}

	/* __progname must equal argv[0] (full path). */
	if (!__progname || strcmp(__progname, argv[0]) != 0) {
		printf("FAIL __progname=%s argv[0]=%s\n",
		       __progname ? __progname : "(null)", argv[0]);
		return 1;
	}

	/* __progname_full must also equal argv[0]. */
	if (!__progname_full || strcmp(__progname_full, argv[0]) != 0) {
		printf("FAIL __progname_full=%s argv[0]=%s\n",
		       __progname_full ? __progname_full : "(null)", argv[0]);
		return 1;
	}

	/* program_invocation_name must equal argv[0]. */
	if (!program_invocation_name ||
	    strcmp(program_invocation_name, argv[0]) != 0) {
		printf("FAIL program_invocation_name=%s argv[0]=%s\n",
		       program_invocation_name ? program_invocation_name : "(null)",
		       argv[0]);
		return 1;
	}

	/* program_invocation_short_name must be the basename of argv[0]. */
	{
		const char *bn = basename_only(argv[0]);
		if (!program_invocation_short_name ||
		    strcmp(program_invocation_short_name, bn) != 0) {
			printf("FAIL short_name=%s want=%s\n",
			       program_invocation_short_name
			       ? program_invocation_short_name : "(null)", bn);
			return 1;
		}
	}

	/* getauxval(AT_PAGESZ) must return a positive multiple of 4096. */
	errno = 0;
	pagesz = getauxval(AT_PAGESZ);
	if (pagesz == 0 || pagesz % 4096 != 0) {
		printf("FAIL getauxval(AT_PAGESZ)=%lu errno=%d\n", pagesz, errno);
		return 1;
	}

	/* getauxval(AT_RANDOM) must return a non-zero address. */
	errno = 0;
	random = getauxval(AT_RANDOM);
	if (random == 0) {
		printf("FAIL getauxval(AT_RANDOM)=0 errno=%d\n", errno);
		return 1;
	}

	puts("PASS p0 entry contract");
	return 0;
}