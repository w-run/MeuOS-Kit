/* locale_gate.c — locale family fine-grained gate (setlocale/localeconv/
 * strcoll/strxfrm).
 *
 * Verifies C/POSIX locale semantics: setlocale category/name handling
 * (with "POSIX" normalized to "C", "" reading $LC_ALL/$LANG precedent,
 * unsupported names rejected with NULL), localeconv()'s C-locale values,
 * strcoll() reducing to byte collation, and strxfrm()'s identity transform
 * (returns strlen(src); copies up to n-1 bytes + NUL).  strcoll/strxfrm
 * previously had no implementation at all — this gate pins their contract. */
#include <locale.h>
#include <string.h>
#include <stdio.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

static void
chk_str(const char *lbl, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL: %s got=\"%s\" want=\"%s\"\n", lbl, got, want);
		fails++;
	}
}

int
main(void)
{
	/* setlocale query returns "C" by default */
	chk_str("setlocale(NULL)", setlocale(LC_ALL, NULL), "C");

	/* set to C / POSIX (normalized to "C") */
	chk_str("setlocale C", setlocale(LC_ALL, "C"), "C");
	chk_str("setlocale POSIX->C", setlocale(LC_ALL, "POSIX"), "C");

	/* "" reads environment; must return something non-NULL, and the
	 * environment may or may not be set, so just require a valid string */
	chk("setlocale empty", setlocale(LC_ALL, "") != NULL);

	/* unsupported locale name rejected */
	chk("setlocale bad", setlocale(LC_ALL, "fr_FR.UTF-8") == NULL);

	/* localeconv C-locale values */
	{
		struct lconv *lc = localeconv();
		chk("lconv nonnull", lc != NULL);
		chk_str("decimal_point", lc->decimal_point, ".");
		chk("mon_decimal_point empty", lc->mon_decimal_point[0] == '\0');
		chk("int_frac_digits -1", lc->int_frac_digits == -1);
		chk("frac_digits -1", lc->frac_digits == -1);
		chk("currency empty", lc->currency_symbol[0] == '\0');
	}

	/* strcoll: C-locale byte-order comparison */
	setlocale(LC_COLLATE, "C");
	chk("strcoll eq", strcoll("abc", "abc") == 0);
	chk("strcoll lt", strcoll("abc", "abd") < 0);
	chk("strcoll gt", strcoll("bcd", "abc") > 0);
	chk("strcoll prefix", strcoll("ab", "abc") < 0);

	/* strxfrm: identity transform, returns strlen(src) */
	{
		char dst[32];
		chk("strxfrm len", strxfrm(dst, "hello", sizeof dst) == 5);
		chk_str("strxfrm copy", dst, "hello");

		/* truncated write: n-1 bytes + NUL, still returns full len */
		memset(dst, 'X', sizeof dst);
		chk("strxfrm trunc len", strxfrm(dst, "abcdefghij", 5) == 10);
		chk_str("strxfrm trunc copy", dst, "abcd");

		/* n==0: no write, returns len */
		memset(dst, 'Y', sizeof dst);
		chk("strxfrm n0 len", strxfrm(dst, "abc", 0) == 3);
		chk("strxfrm n0 no-write", dst[0] == 'Y');
	}

	if (fails) {
		printf("%d locale FAIL\n", fails);
		return 1;
	}
	printf("PASS locale\n");
	return 0;
}
