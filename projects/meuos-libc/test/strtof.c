/* strtof / atof / atol / atoll: C99 7.20.1 conversions.
 *
 * Regression guard for the conversion family gap in convert.c (only strtod
 * existed, and it was excluded on i386).  All expected values are exactly
 * representable in float/double, so plain '!=' compares are safe (no
 * double-rounding ambiguity for these inputs).
 */
#include <stdio.h>
#include <stdlib.h>

static int
checkf(const char *s, float want)
{
	char *end;
	float got = strtof(s, &end);
	if (got != want) {
		printf("FAIL: strtof(\"%s\") = %g, want %g\n", s, (double)got, (double)want);
		return 1;
	}
	return 0;
}

static int
checkd(const char *s, double want)
{
	double got = atof(s);
	if (got != want) {
		printf("FAIL: atof(\"%s\") = %g, want %g\n", s, got, want);
		return 1;
	}
	return 0;
}

int
main(void)
{
	char *end;

	/* strtof basics */
	if (checkf("3.5", 3.5f)) return 1;
	if (checkf("-2.25", -2.25f)) return 2;
	if (checkf("+0.5", 0.5f)) return 3;
	if (checkf("1e2", 100.0f)) return 4;
	if (checkf("0.125e2", 12.5f)) return 5;
	/* strtof accepts C99 hex floats too (shared parse path) */
	if (checkf("0x1p4", 16.0f)) return 6;
	if (checkf("0x1.8p3", 12.0f)) return 7;
	/* end pointer stops after the constant */
	if (strtof("1.5x", &end) != 1.5f || *end != 'x') return 8;
	/* trailing junk after a hex float */
	if (strtof("0x1p3z", &end) != 8.0f || *end != 'z') return 9;

	/* atof (strtod with NULL end) */
	if (checkd("3.25", 3.25)) return 10;
	if (checkd("  12.5e1", 125.0)) return 11;
	if (checkd("-0.5", -0.5)) return 12;
	if (checkd("0x1p2", 4.0)) return 13;

	/* atol / atoll thin wrappers over strtol/strtoll */
	if (atol(" -42") != -42L) return 14;
	if (atol("42") != 42L) return 15;
	if (atoll("1234567890123") != 1234567890123LL) return 16;
	if (atoll("-987654321") != -987654321LL) return 17;

	/* strtod still intact (decimal + hex) */
	if (strtod("2.5", &end) != 2.5 || *end != '\0') return 18;
	if (strtod("0x1p-1", &end) != 0.5 || *end != '\0') return 19;

	puts("PASS strtof/atof/atol/atoll");
	return 0;
}
