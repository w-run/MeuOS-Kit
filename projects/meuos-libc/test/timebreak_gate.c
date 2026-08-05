/* timebreak_gate.c — gmtime/mktime/asctime/ctime/difftime fine-grained gate.
 *
 * Verifies the broken-down time conversion against known epoch instants with
 * exact field and formatted-text assertions, the mktime<->gmtime round trip,
 * and difftime's second difference.  Isolates time-decomposition failures
 * that the coarse time.c would mask. */
#include <time.h>
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
chk_int(const char *fld, int got, int want)
{
	if (got != want) {
		printf("FAIL: gmtime %s=%d want %d\n", fld, got, want);
		fails++;
	}
}

int
main(void)
{
	struct tm *tm;
	time_t t;

	/* epoch 0 -> 1970-01-01 00:00:00 UTC (Thu, yday 0) */
	t = 0;
	tm = gmtime(&t);
	chk("gmtime(0)", tm != NULL);
	chk_int("sec",  tm->tm_sec, 0);
	chk_int("min",  tm->tm_min, 0);
	chk_int("hour", tm->tm_hour, 0);
	chk_int("mday", tm->tm_mday, 1);
	chk_int("mon",  tm->tm_mon, 0);
	chk_int("year", tm->tm_year, 70);
	chk_int("wday", tm->tm_wday, 4);    /* Thu */
	chk_int("yday", tm->tm_yday, 0);

	/* a known post-epoch instant: 2000-01-01T00:00:00Z = 946684800 */
	t = 946684800;
	tm = gmtime(&t);
	chk_int("2000 mday", tm->tm_mday, 1);
	chk_int("2000 mon",  tm->tm_mon, 0);
	chk_int("2000 year", tm->tm_year, 100);
	chk_int("2000 wday", tm->tm_wday, 6);   /* Sat */

	/* pre-epoch: -86400 is 1969-12-31T00:00:00Z */
	t = -86400;
	tm = gmtime(&t);
	chk_int("-86400 mday", tm->tm_mday, 31);
	chk_int("-86400 mon",  tm->tm_mon, 11);
	chk_int("-86400 year", tm->tm_year, 69);

	/* mktime round trip: mktime(gmtime(t)) == t */
	t = 1699999999;   /* some arbitrary second */
	tm = gmtime(&t);
	time_t back = mktime(tm);
	chk("roundtrip mktime(gmtime(t))==t", back == t);

	/* mktime field normalization: decode a full struct, mktime fills wday */
	{
		struct tm ft = { 0 };
		ft.tm_year = 100; ft.tm_mon = 0; ft.tm_mday = 1;
		back = mktime(&ft);
		chk("mktime 2000-01-01", back == 946684800);
		chk("mktime fills wday", ft.tm_wday == 6);
		chk("mktime fills yday", ft.tm_yday == 0);
	}

	/* difftime */
	chk("difftime",  difftime(200, 100) == 100);
	chk("difftime neg", difftime(99, 100) == -1);

	/* asctime exact fixed-width text */
	t = 0;
	tm = gmtime(&t);
	char *a = asctime(tm);
	chk("asctime nonnull", a != NULL);
	/* asctime is "Www Mmm dd hh:mm:ss yyyy\n": Thu Jan  1 00:00:00 1970 */
	chk("asctime text", a && strcmp(a, "Thu Jan  1 00:00:00 1970\n") == 0);

	/* ctime(0) matches asctime(gmtime(0)) in a local=UTC build */
	char *c = ctime(&t);
	chk("ctime == asctime(gmtime)", c && a && strcmp(c, a) == 0);

	if (fails) {
		printf("%d timebreak FAIL\n", fails);
		return 1;
	}
	printf("PASS timebreak\n");
	return 0;
}
