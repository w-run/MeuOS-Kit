/* time.h: C99 7.23 functions -- difftime/asctime/ctime/clock + gmtime/mktime.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

int
main(void)
{
	time_t t = 0;	/* 1970-01-01 00:00:00 UTC, Thursday */
	struct tm *utc = gmtime(&t);
	char buf[64];

	if (!utc || utc->tm_year != 70 || utc->tm_mon != 0 || utc->tm_mday != 1
	 || utc->tm_hour != 0 || utc->tm_min != 0 || utc->tm_sec != 0
	 || utc->tm_wday != 4)	/* Thursday */
		return 1;

	if (!strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", utc)
	 || strcmp(buf, "1970-01-01 00:00:00") != 0)
		return 2;

	/* asctime fixed format */
	{
		char *a = asctime(utc);
		if (!a || strcmp(a, "Thu Jan  1 00:00:00 1970\n") != 0)
			return 3;
	}
	{
		char *c = ctime(&t);
		if (!c || strcmp(c, "Thu Jan  1 00:00:00 1970\n") != 0)
			return 4;
	}
	if (difftime(100, 40) != 60.0)
		return 5;
	if (difftime(0, 100) != -100.0)
		return 6;

	/* mktime round-trips gmtime */
	{
		struct tm tm = {0, 0, 12, 1, 0, 70, 0, 0, 0};	/* 1970-01-01 12:00:00 */
		time_t r = mktime(&tm);
		if (r != 12 * 3600)
			return 7;
	}

	/* clock() is positive monotonic-ish; exact value depends on the host */
	if (clock() < 0)
		return 8;

	puts("PASS time");
	return 0;
}
