/* wchar_io2_gate.c — wide string I/O + wcstok (C11 7.29.3/7.29.4.5.7).
 *
 * Exercises fgetws/fputws/getwchar/putwchar and the thread-safe wcstok
 * tokeniser.  Wide values are built as integers (no wide-literal lowering
 * dependency).  Stream round-trips use tmpfile(); fputws to stdout validates
 * the stdout-backed path. */
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

int main(void)
{
	/* --- fputws / fgetws round trip on a tmpfile --- */
	{
		FILE *f = tmpfile();
		if (!f) { printf("FAIL: tmpfile\n"); return 1; }
		wchar_t line1[] = { 'h','e','l','l','o','\n',0 };
		wchar_t line2[] = { 'w','o','r','l','d',0 };
		chk("fputws l1", fputws(line1, f) != WEOF);
		chk("fputws l2", fputws(line2, f) != WEOF);
		rewind(f);
		wchar_t buf[16];
		chk("fgetws l1", fgetws(buf, 16, f) == buf &&
		    wcscmp(buf, line1) == 0);
		chk("fgetws l2", fgetws(buf, 16, f) == buf &&
		    wcscmp(buf, line2) == 0);
		chk("fgetws eof", fgetws(buf, 16, f) == NULL);
		fclose(f);
	}

	/* --- fputws to stdout returns a non-negative value --- */
	{
		wchar_t msg[] = { 'x',0 };
		chk("fputws stdout", fputws(msg, stdout) != WEOF);
	}

	/* --- putwchar writes a wide char to stdout --- */
	chk("putwchar Z", putwchar((wchar_t)'Z') == (wint_t)'Z');

	/* --- wcstok tokenises a wide string on a delimiter --- */
	{
		wchar_t s[] = { 'a',',','b','b',',','c',0 };
		wchar_t delim[] = { ',',0 };
		wchar_t *state = NULL;
		wchar_t *t;
		t = wcstok(s, delim, &state);
		chk("tok1", t && t[0]=='a' && t[1]==0);
		t = wcstok(NULL, delim, &state);
		chk("tok2", t && t[0]=='b' && t[1]=='b' && t[2]==0);
		t = wcstok(NULL, delim, &state);
		chk("tok3", t && t[0]=='c' && t[1]==0);
		t = wcstok(NULL, delim, &state);
		chk("tok end", t == NULL);
	}

	if (fails) {
		printf("%d wchar_io2 FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_io2\n");
	return 0;
}
