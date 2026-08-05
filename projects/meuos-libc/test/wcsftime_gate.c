/* wcsftime_gate.c — wide time formatting (C11 7.29.5.1).
 *
 * Verifies wcsftime() against a fixed struct tm across the common %-directives
 * and overflow (buffer-too-small -> 0).  Format strings are built as integer
 * arrays (no wide-literal dependency).  Every case compares wcsftime's wide
 * output byte-for-byte against the narrow strftime() result, so the two stay
 * consistent. */
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

/* wcsftime a narrow format (converted per-element) and compare to strftime. */
static void
t_case(const char *lbl, const char *fmt_c, const struct tm *t)
{
	wchar_t wb[128], wf[128];
	char exp[128], got[128];
	size_t i, wn;

	for (i = 0; fmt_c[i]; i++)
		wf[i] = (wchar_t)(unsigned char)fmt_c[i];
	wf[i] = L'\0';

	wn = wcsftime(wb, 128, wf, t);
	/* render must not exceed the wide buffer */
	if (wn >= 128) { chk(lbl, 0); return; }
	for (i = 0; i < wn; i++)
		got[i] = (char)wb[i];
	got[i] = '\0';

	strftime(exp, sizeof exp, fmt_c, t);
	chk(lbl, wn == strlen(exp) && strcmp(got, exp) == 0);
}

int main(void)
{
	struct tm t, utc;
	time_t base = 1709176029; /* 2024-02-29 13:07:09 UTC */

	if (!gmtime_r(&base, &utc)) {
		printf("FAIL: gmtime_r\n");
		return 1;
	}
	t = utc;

	t_case("wcsf Y-m-d H:M:S", "%Y-%m-%d %H:%M:%S", &t);
	t_case("wcsf F",           "%F", &t);
	t_case("wcsf F a b",       "%F %a %b", &t);
	t_case("wcsf I p",         "%I %p", &t);
	t_case("wcsf T",           "%T", &t);
	t_case("wcsf j d",         "%j %d", &t);
	t_case("wcsf W C y",       "%W %C %y", &t);
	t_case("wcsf c",           "%c", &t);
	t_case("wcsf literal",     "DATE: %F %R", &t);

	/* Overflow: tiny buffer cannot hold result incl NUL -> 0 */
	{
		static const wchar_t f[] = { L'%',L'Y',L'-',L'%',L'm',L'-',L'%',L'd',0 };
		wchar_t tiny[4];
		chk("wcsf overflow 0", wcsftime(tiny, 4, f, &t) == 0);
	}
	/* maxsize 0 -> 0 */
	{
		static const wchar_t f[] = { L'%',L'Y',0 };
		wchar_t b[8];
		chk("wcsf max0", wcsftime(b, 0, f, &t) == 0);
	}

	if (fails) {
		printf("%d wcsftime FAIL\n", fails);
		return 1;
	}
	printf("PASS wcsftime\n");
	return 0;
}
