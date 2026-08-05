/* wprintf_gate.c — wide formatted output family (C11 7.29.2.3).
 *
 * Exercises swprintf (in-memory wchar_t buffer) + fwprintf/wprintf (stream)
 * against fixed wide format strings: %lc/%ls/%C/%S, integers, floats, width,
 * precision, and %n.  Wide output is compared against a wchar expectation
 * array (in C locale the fields are ASCII). */
#include <wchar.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

static int weq(const wchar_t *a, const wchar_t *b)
{
	for (; *a && *b; a++, b++)
		if (*a != *b) return 0;
	return *a == *b;
}

int main(void)
{
	wchar_t buf[128];

	/* %ls wide-string + %d integer + width */
	{
		int n = swprintf(buf, 128, L"[%ls-%d]",
			L"hello", 42);
		chk("swp ls/d", n == 10 && weq(buf, L"[hello-42]"));
	}
	/* %ls precision truncation */
	{
		int n = swprintf(buf, 128, L"%.3ls", L"abcdef");
		chk("swp .ls", n == 3 && weq(buf, L"abc"));
	}
	/* %lc wide char, %C alias, %c plain int-with-precision */
	{
		int n = swprintf(buf, 128, L"%lc|%C|%c", (wint_t)L'x', (wint_t)L'y', 'z');
		chk("swp lc/C/c", n == 5 && weq(buf, L"x|y|z"));
	}
	/* %ls width + left/right alignment */
	{
		int n = swprintf(buf, 128, L"[%5ls][%-5ls]", L"ab", L"ab");
		chk("swp ls width", n == 14 && weq(buf, L"[   ab][ab   ]"));
	}
	/* %d padding & base */
	{
		int n = swprintf(buf, 128, L"%04d|%x|%f", 42, 255, 3.5);
		chk("swp d/x/f", n == 16 && weq(buf, L"0042|ff|3.500000"));
	}
	/* %n records wide-char count */
	{
		int written = -1;
		int n = swprintf(buf, 128, L"abc%n", &written);
		chk("swp %n ret", n == 3);
		chk("swp %n count", written == 3);
	}
	/* %S alias for wide string */
	{
		int n = swprintf(buf, 128, L"%S", L"wide!");
		chk("swp %S", n == 5 && weq(buf, L"wide!"));
	}
	/* \0 termination and out-of-size safety */
	{
		wchar_t small[4];
		swprintf(small, 4, L"abcdef");
		chk("swp trunc", small[0] == L'a' && small[1] == L'b' && small[3] == L'\0');
	}

	/* fwprintf to a tmpfile, read back */
	{
		FILE *f = tmpfile();
		if (!f) { printf("FAIL: tmpfile\n"); return 1; }
		int n = fwprintf(f, L"%ls = %04d\n", L"n", 7);
		chk("fwprintf ret", n == 9);
		rewind(f);
		wchar_t line[64];
		chk("fwprintf getws", fgetws(line, 64, f) == line &&
		    weq(line, L"n = 0007\n"));
		fclose(f);
	}

	if (fails) {
		printf("wprintf_gate: %d FAIL\n", fails);
		return 1;
	}
	printf("PASS wprintf\n");
	return 0;
}
