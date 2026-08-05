/* strfmon_gate.c — POSIX strfmon regression gate.
 * C locale: currency symbols empty, frac_digits -1 (default precision 2),
 * mon_decimal_point empty (uses '.'), no grouping. */
#include <money.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void
chk(const char *fmt, int want, double v, const char *expected)
{
	char buf[128];
	ssize_t n = strfmon(buf, sizeof buf, fmt, v);
	if (n < 0) {
		printf("FAIL %s -> overflow\n", fmt);
		fails++;
		return;
	}
	if (strcmp(buf, expected) != 0) {
		printf("FAIL %s(%g) = '%s' want '%s'\n", fmt, v, buf, expected);
		fails++;
		return;
	}
	(void)want;
}

int
main(void)
{
	/* default precision 2, C locale */
	chk("%.2n", 0, 1234.5, "1234.50");
	chk("%n", 0, 1234.5, "1234.50");            /* default precision = 2 */
	chk("%.0n", 0, 1234.5, "1235");             /* round to integer */
	chk("%.2n", 0, 99.99, "99.99");
	chk("%.2n", 0, 0.0, "0.00");
	/* international symbol: empty in C locale, so bare number */
	chk("%.2i", 0, 1234.5, "1234.50");
	/* suppress currency symbol */
	chk("%!n", 0, 999.99, "999.99");
	/* negative in parentheses */
	chk("%(n)", 0, -5.0, "(5.00)");
	/* positive: '(' flag opens no paren, but the literal ')' in the format
	 * is still emitted verbatim (POSIX/glibc behavior) */
	chk("%(n)", 0, 5.0, "5.00)");
	/* literal percent */
	chk("%%", 0, 0.0, "%");

	if (fails) {
		printf("%d strfmon FAIL\n", fails);
		return 1;
	}
	printf("PASS strfmon\n");
	return 0;
}
