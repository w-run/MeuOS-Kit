/* strptime_gate.c — strptime regression gate.
 * Asserts parsing of common formats: %Y-%m-%d, %H:%M:%S, %b %d %Y, the full
 * datetime, %A weekday, and that a mismatch returns NULL.  Also verifies a
 * strftime/strptime round-trip. */
#include <time.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void
chk_full(const char *label, const char *str, const char *fmt,
         int Y, int mo, int d, int H, int M, int S)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	char *r = strptime(str, fmt, &t);
	if (!r) { printf("FAIL %s: strptime(NULL) for '%s' '%s'\n", label, str, fmt); fails++; return; }
	int yy = t.tm_year + 1900, mm = t.tm_mon + 1;
	if (Y >= 0 && yy != Y) { printf("FAIL %s: year=%d want %d\n", label, yy, Y); fails++; }
	if (mo >= 0 && mm != mo) { printf("FAIL %s: mon=%d want %d\n", label, mm, mo); fails++; }
	if (d >= 0 && t.tm_mday != d) { printf("FAIL %s: mday=%d want %d\n", label, t.tm_mday, d); fails++; }
	if (H >= 0 && t.tm_hour != H) { printf("FAIL %s: hour=%d want %d\n", label, t.tm_hour, H); fails++; }
	if (M >= 0 && t.tm_min != M) { printf("FAIL %s: min=%d want %d\n", label, t.tm_min, M); fails++; }
	if (S >= 0 && t.tm_sec != S) { printf("FAIL %s: sec=%d want %d\n", label, t.tm_sec, S); fails++; }
}

int
main(void)
{
	chk_full("date",   "2024-03-15",       "%Y-%m-%d",       2024, 3, 15, -1, -1, -1);
	chk_full("time",   "23:45:06",         "%H:%M:%S",       -1, -1, -1, 23, 45, 6);
	chk_full("mon",    "Mar 15 2024",      "%b %d %Y",       2024, 3, 15, -1, -1, -1);
	chk_full("month",  "March 15 2024",    "%B %d %Y",       2024, 3, 15, -1, -1, -1);
	chk_full("full",   "1970-01-01 00:00:01", "%Y-%m-%d %H:%M:%S", 1970, 1, 1, 0, 0, 1);
	chk_full("dayabbr","Sun",              "%a",             -1, -1, -1, -1, -1, -1);
	/* weekday names */
	{
		struct tm t; memset(&t, 0, sizeof t);
		if (!strptime("Sunday", "%A", &t)) { printf("FAIL: Sunday\n"); fails++; }
		else if (t.tm_wday != 0) { printf("FAIL: Sunday wday=%d want 0\n", t.tm_wday); fails++; }
		if (!strptime("Wednesday", "%A", &t)) { printf("FAIL: Wed\n"); fails++; }
		else if (t.tm_wday != 3) { printf("FAIL: Wed wday=%d want 3\n", t.tm_wday); fails++; }
	}
	/* mismatch -> NULL */
	{
		struct tm t; memset(&t, 0, sizeof t);
		if (strptime("not-a-date", "%Y-%m-%d", &t)) { printf("FAIL: mismatch should be NULL\n"); fails++; }
	}
	/* strftime <-> strptime round-trip */
	{
		struct tm t; memset(&t, 0, sizeof t);
		t.tm_year = 124; t.tm_mon = 11; t.tm_mday = 25; t.tm_hour = 8; t.tm_min = 30; t.tm_sec = 0;
		char buf[64];
		if (!strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &t)) { printf("FAIL: strftime\n"); fails++; }
		struct tm t2; memset(&t2, 0, sizeof t2);
		if (!strptime(buf, "%Y-%m-%d %H:%M:%S", &t2)) { printf("FAIL: roundtrip strptime\n"); fails++; }
		else if (t2.tm_year != 124 || t2.tm_mon != 11 || t2.tm_mday != 25 ||
		         t2.tm_hour != 8 || t2.tm_min != 30) {
			printf("FAIL: roundtrip mismatch\n"); fails++;
		}
	}

	if (fails) {
		printf("%d strptime FAIL\n", fails);
		return 1;
	}
	printf("PASS strptime\n");
	return 0;
}
