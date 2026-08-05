/* wchar_num_gate.c — wide numeric conversion (C11 7.29.4.1).
 *
 * Exercises wcstol/wcstoul/wcstoll/wcstoull: decimal/hex/base-0 parsing,
 * sign handling, overflow (ERANGE + saturation), and endptr placement.
 * Wide strings are built as integer arrays (no wide-literal dependency). */
#include <wchar.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

static int fails;

static void chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

int main(void)
{
	/* wcstoul decimal */
	{
		wchar_t s[] = { '1','2','3',0 };
		wchar_t *e;
		chk("wcstoul 123", wcstoul(s, &e, 10) == 123UL && *e == 0);
	}
	/* wcstoul hex via base 16 */
	{
		wchar_t s[] = { '0','x','f','f',0 };
		wchar_t *e;
		chk("wcstoul 0xff", wcstoul(s, &e, 16) == 255UL && *e == 0);
		chk("wcstoul 0xff b0", wcstoul(s, &e, 0) == 255UL && *e == 0);
	}
	/* wcstoul leading whitespace + sign + base 0 auto (octal) */
	{
		wchar_t s[] = { ' ',' ','+','0','7','7',0 };
		wchar_t *e;
		chk("wcstoul octal", wcstoul(s, &e, 0) == 077UL && *e == 0);
	}
	/* wcstoul overflow -> ULONG_MAX + ERANGE */
	{
		wchar_t s[] = { '9','9','9','9','9','9','9','9','9','9','9','9','9','9','9','9','9','9','9','9',0 };
		wchar_t *e;
		errno = 0;
		chk("wcstoul ovf", wcstoul(s, &e, 10) == (unsigned long)-1);
		chk("wcstoul ovf errno", errno == ERANGE);
	}
	/* wcstol negative */
	{
		wchar_t s[] = { '-','4','2',0 };
		wchar_t *e;
		chk("wcstol -42", wcstol(s, &e, 10) == -42L && *e == 0);
	}
	/* wcstol endptr points past digits */
	{
		wchar_t s[] = { '1','2','z',0 };
		wchar_t *e;
		chk("wcstol 12z", wcstol(s, &e, 10) == 12L);
		chk("wcstol end", e == s + 2);
	}
	/* wcstoll large */
	{
		wchar_t s[] = { '1','2','3','4','5','6','7','8','9','1','2','3','4','5','6','7','8','9',0 };
		wchar_t *e;
		chk("wcstoll big", wcstoll(s, &e, 10) == 123456789123456789LL && *e == 0);
	}
	/* wcstoull parse a full-width uint64 magnitude (9.2e18 < 2^64) */
	{
		wchar_t s[] = { '9','2','2','3','3','7','2','0','3','6','8','5','4','7','7','5','8','0','7',0 };
		wchar_t *e;
		chk("wcstoull big", wcstoull(s, &e, 10) == 9223372036854775807ULL && *e == 0);
		/* ULLONG_MAX path: parse all-9s overflows to (unsigned)-1 */
		wchar_t s2[] = { '1','8','4','4','6','7','4','4','0','7','3','7','0','9','5','5','1','6','1','5',0 };
		wchar_t *e2;
		errno = 0;
		chk("wcstoull max", wcstoull(s2, &e2, 10) == (unsigned long long)-1);
	}

	if (fails) {
		printf("%d wchar_num FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_num\n");
	return 0;
}
