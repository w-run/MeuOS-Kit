/* strtod: C99 hexadecimal floating constants (6.4.4.2).
 *
 * Regression guard: meuos-libc's strtod used to stop at '0' on "0x1p63",
 * which broke the self-hosted mcc compiling any source containing a hex
 * float (parse_hex_float added in convert.c).  Also covers the plain
 * hex-integer fallback ("0x10" -> 16, no p/P exponent).
 *
 * All expected values are exactly representable as doubles, so plain
 * '!=' compares are safe.
 */
#include <stdio.h>
#include <stdlib.h>

static int
check(const char *s, double want)
{
	char *end;
	double got = strtod(s, &end);
	if (got != want) {
		printf("FAIL: strtod(\"%s\") = %g, want %g (end at '%c')\n",
		       s, got, want, *end);
		return 1;
	}
	return 0;
}

int
main(void)
{
	char *end;

	/* 2^63, the exact value that broke self-hosted mcc */
	if (check("0x1p63", 9223372036854775808.0))
		return 1;
	/* mantissa fraction + positive exponent */
	if (check("0x1.8p3", 12.0))
		return 2;
	/* negative binary exponent */
	if (check("0x1p-2", 0.25))
		return 3;
	/* leading '-' handled by the sign path */
	if (check("-0x1p4", -16.0))
		return 4;
	/* uppercase 0X/P, signed exponent */
	if (check("0X1P+3", 8.0))
		return 5;
	/* lowercase 0x, uppercase P, negative exponent */
	if (check("0x1.8P-2", 0.375))
		return 6;
	/* hex digit 'f' in mantissa, multi-digit exponent */
	if (check("0x1.fp10", 1984.0))
		return 7;
	/* 15-digit mantissa (exact binary accumulation) */
	if (check("0x1.921fb54442d18p1", 3.141592653589793))
		return 8;
	/* plain hex integer without p/P falls back to integer parse */
	if (check("0x10", 16.0))
		return 9;
	if (strtod("0x10", &end) != 16.0 || *end != '\0')
		return 10;
	/* non-numeric suffix: end must stop right after the constant */
	if (strtod("0x1p3z", &end) != 8.0 || *end != 'z')
		return 11;
	/* "0x1.8" has no binary exponent: value 1.0, end at '.' like glibc */
	if (strtod("0x1.8", &end) != 1.0 || *end != '.')
		return 12;

	puts("PASS strtod hex float");
	return 0;
}
