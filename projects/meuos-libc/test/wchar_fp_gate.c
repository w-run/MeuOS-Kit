/* wchar_fp_gate.c — wide floating-point conversion (C11 7.29.4.1.1/.3).
 *
 * Exercises wcstod/wcstof: decimal, negative, exponent, leading whitespace,
 * and endptr placement.  Wide strings are integer arrays (no wide-literal
 * dependency). */
#include <wchar.h>
#include <stdio.h>
#include <math.h>

static int fails;

static void chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

static int approx(double a, double b)
{
	double d = a - b;
	if (d < 0) d = -d;
	return d < 1e-9;
}

int main(void)
{
	/* wcstod decimal */
	{
		wchar_t s[] = { '3','.','1','4',0 };
		wchar_t *e;
		chk("wcstod pi", approx(wcstod(s, &e), 3.14) && *e == 0);
	}
	/* wcstod negative */
	{
		wchar_t s[] = { '-','2','.','5',0 };
		wchar_t *e;
		chk("wcstod neg", approx(wcstod(s, &e), -2.5) && *e == 0);
	}
	/* wcstod exponent */
	{
		wchar_t s[] = { '1','e','3',0 };
		wchar_t *e;
		chk("wcstod exp", approx(wcstod(s, &e), 1000.0) && *e == 0);
	}
	/* leading whitespace */
	{
		wchar_t s[] = { ' ',' ','0','.','5',0 };
		wchar_t *e;
		chk("wcstod ws", approx(wcstod(s, &e), 0.5) && *e == 0);
	}
	/* endptr points past the number */
	{
		wchar_t s[] = { '1','.','5','x','y',0 };
		wchar_t *e;
		chk("wcstod xy", approx(wcstod(s, &e), 1.5));
		chk("wcstod end", e == s + 3);
	}
	/* wcstof narrows to float */
	{
		wchar_t s[] = { '2','.','2','5',0 };
		wchar_t *e;
		chk("wcstof", approx(wcstof(s, &e), 2.25f) && *e == 0);
	}

	if (fails) {
		printf("%d wchar_fp FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_fp\n");
	return 0;
}
