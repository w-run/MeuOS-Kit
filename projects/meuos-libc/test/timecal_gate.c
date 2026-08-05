/* timecal_gate.c — timegm()/timelocal() regression gate.
 *
 * Verifies UTC conversion: an epoch-second round trip against gmtime_r(),
 * an exact known instant (2000-01-01T00:00:00Z == 946684800), the wday/
 * yday normalization side-effect, and the timelocal()==mktime() identity
 * that holds in this timezone-free (local==UTC) build. */
#include <time.h>
#include <stdio.h>

static int fails;

/* Fill a struct tm from a civil calendar instant.  year is the full year
 * (1900-based stored as tm_year), mon 0-11, mday 1-31, hour/min/sec. */
static void
memset_tm(struct tm *r, int year, int mon, int mday,
    int hour, int min, int sec)
{
	r->tm_year = year - 1900;
	r->tm_mon = mon;
	r->tm_mday = mday;
	r->tm_hour = hour;
	r->tm_min = min;
	r->tm_sec = sec;
	r->tm_isdst = -1;
}

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

int
main(void)
{
	struct tm tm;

	/* 1970-01-01 00:00:00 UTC == 0 */
	memset_tm(&tm, 1970, 0, 1, 0, 0, 0);
	chk("timegm(1970-01-01) == 0", timegm(&tm) == 0);
	chk("gmtime(0).tm_wday == 4 (Thu)", tm.tm_wday == 4);
	chk("gmtime(0).tm_yday == 0", tm.tm_yday == 0);

	/* exact known instant: 2000-01-01T00:00:00Z -> 946684800 */
	memset_tm(&tm, 2000, 0, 1, 0, 0, 0);
	chk("timegm(2000-01-01) == 946684800", timegm(&tm) == 946684800);

	/* round trip: timegm -> gmtime_r -> timegm identical */
	memset_tm(&tm, 2023, 5, 15, 12, 34, 56);   /* 2023-06-15 12:34:56 */
	time_t a = timegm(&tm);
	struct tm out;
	gmtime_r(&a, &out);
	time_t b = timegm(&out);
	chk("roundtrip timegm==gmtime_r==timegm", a == b);
	chk("roundtrip seconds positive", a > 0);

	/* timelocal() == mktime() in this local==UTC build */
	memset_tm(&tm, 2024, 11, 31, 23, 59, 59);  /* 2024-12-31 23:59:59 */
	chk("timelocal == mktime", timelocal(&tm) == mktime(&tm));
	/* and that value is 2025-01-01T00:00:59Z ... verify via gmtime roundtrip */
	time_t tload = timelocal(&tm);
	struct tm back;
	gmtime_r(&tload, &back);
	chk("timelocal(2024-12-31 23:59:59Z) ok", back.tm_mon == 11 && back.tm_mday == 31);

	if (fails) {
		printf("%d timecal FAIL\n", fails);
		return 1;
	}
	printf("PASS timecal\n");
	return 0;
}
