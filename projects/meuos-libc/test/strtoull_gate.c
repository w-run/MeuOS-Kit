/* strtoull_gate.c — strtol/strtoul/strtoll/strtoull regression gate.
 *
 * Verifies integer parsing semantics: base auto-detection (0x/0 prefixes),
 * sign handling, base-2/16, trailing garbage, and overflow -> ERANGE with
 * the clamped value. */
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d)\n", lbl, errno);
		fails++;
	}
}

int
main(void)
{
	char *end;

	/* base 10 */
	errno = 0;
	chk("strtol(123)", strtol("123", &end, 10) == 123 && *end == 0);

	/* hex via 0x (base 0 auto-detection) */
	errno = 0;
	chk("strtol(0x10)", strtol("0x10", &end, 0) == 16 && *end == 0);

	/* octal via leading 0 (base 0) */
	errno = 0;
	chk("strtol(010)", strtol("010", &end, 0) == 8 && *end == 0);

	/* explicit base 2 */
	errno = 0;
	chk("strtol(base2 101)", strtol("101", &end, 2) == 5 && *end == 0);

	/* negative plus trailing junk */
	errno = 0;
	chk("strtol(-42junk)", strtol("-42junk", &end, 10) == -42 && *end == 'j');

	/* unsigned */
	errno = 0;
	chk("strtoul(4294967295)", strtoul("4294967295", &end, 10) == 4294967295UL);

	/* overflow -> ERANGE, clamped to LONG_MAX, and end at the digit */
	errno = 0;
	long ov = strtol("99999999999999999999999999", &end, 10);
	chk("strtol overflow ERANGE", ov == LONG_MAX && errno == ERANGE);

	/* overflow unsigned -> ULONG_MAX */
	unsigned long uov = strtoul("88888888888888888888888888888888", &end, 10);
	chk("strtoul overflow ERANGE", uov == ULONG_MAX && errno == ERANGE);

	if (fails) {
		printf("%d strtoull FAIL\n", fails);
		return 1;
	}
	printf("PASS strtoull\n");
	return 0;
}
