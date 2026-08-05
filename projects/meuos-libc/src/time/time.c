/* time/time.c — POSIX time functions */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/times.h>

/* Days in month for non-leap and leap years */
static const int days_in_mon[2][12] = {
	{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
	{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

static int is_leap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* Days from 1970-01-01 to year-01-01 */
static long long year_to_days(int y) {
	long long yd = 0;
	int base = 1970;
	if (y >= base) {
		while (base < y) {
			yd += is_leap(base) ? 366 : 365;
			base++;
		}
	} else {
		while (base > y) {
			base--;
			yd -= is_leap(base) ? 366 : 365;
		}
	}
	return yd;
}

struct tm *
gmtime_r(const time_t *t, struct tm *r)
{
	long long secs = (long long)*t;
	long long days, epoch_days;
	int y, m, d, h, min, s, wday, yday;

	/* Break down seconds using floor division so negative time_t
	 * (before 1970) yields non-negative seconds/minutes/hours. */
	s = (int)(secs % 60); secs /= 60;
	if (s < 0) { s += 60; secs--; }
	min = (int)(secs % 60); secs /= 60;
	if (min < 0) { min += 60; secs--; }
	h = (int)(secs % 24); secs /= 24;
	if (h < 0) { h += 24; secs--; }

	/* days since epoch (floor). */
	days = secs;
	epoch_days = days;

	/* Year: step forward for non-negative days, backward (into the
	 * pre-1970 range) for negative days. */
	y = 1970;
	while (1) {
		int leap = is_leap(y);
		int ydays = leap ? 366 : 365;
		if (days >= 0 && days < ydays)
			break;
		if (days >= ydays) {
			days -= ydays;
			y++;
		} else {
			y--;
			days += is_leap(y) ? 366 : 365;
		}
	}

	/* Day of year (0-based) before the month loop consumes it. */
	yday = (int)days;

	/* Month */
	int leap = is_leap(y);
	for (m = 0; m < 12; m++) {
		if (days < days_in_mon[leap][m]) break;
		days -= days_in_mon[leap][m];
	}
	d = (int)days + 1; /* 1-based */

	/* Day of week: 1970-01-01 was Thursday (4); epoch_days is the
	 * floor day count so negative values also land correctly. */
	wday = (int)((epoch_days + 4) % 7);
	if (wday < 0) wday += 7;

	r->tm_sec = s;
	r->tm_min = min;
	r->tm_hour = h;
	r->tm_mday = d;
	r->tm_mon = m;
	r->tm_year = y - 1900;
	r->tm_wday = wday;
	r->tm_yday = yday;
	r->tm_isdst = 0;
	return r;
}

static struct tm tm_buf;

struct tm *
gmtime(const time_t *t)
{
	return gmtime_r(t, &tm_buf);
}

/* Simple localtime: no timezone support, same as gmtime */
struct tm *
localtime_r(const time_t *t, struct tm *r)
{
	return gmtime_r(t, r);
}

struct tm *
localtime(const time_t *t)
{
	return gmtime(t);
}

/* Core broken-down->seconds conversion, interpreting the fields as UTC.
 * Shared by mktime() and timegm(): mktime() will observe local time once
 * timezone support lands, timegm() stays on this UTC path, so the two keep
 * their distinct semantics even though they coincide today (localtime is
 * currently identity with gmtime).  Recomputes wday/yday and leaves isdst
 * indeterminate. */
static time_t
mktime_utc(struct tm *tm)
{
	int y = tm->tm_year + 1900;
	int m = tm->tm_mon;
	int d = tm->tm_mday;

	long long days = year_to_days(y);
	int leap = is_leap(y);
	for (int i = 0; i < m; i++)
		days += days_in_mon[leap][i];
	days += d - 1;

	time_t result = (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);

	/* Recompute wday/yday/isdst */
	struct tm tmp;
	gmtime_r(&result, &tmp);
	tm->tm_wday = tmp.tm_wday;
	tm->tm_yday = tmp.tm_yday;
	tm->tm_isdst = -1;

	return result;
}

time_t
mktime(struct tm *tm)
{
	/* No timezone support yet: local time is UTC. */
	return mktime_utc(tm);
}

/* timegm(): broken-down time interpreted as UTC -> time_t. */
time_t
timegm(struct tm *tm)
{
	return mktime_utc(tm);
}

/* timelocal: broken-down time interpreted as local -> time_t.  This is
 * mktime() under its historical XSI name; they share semantics here. */
time_t
timelocal(struct tm *tm)
{
	return mktime(tm);
}

/* Shared wide formatting core defined in wcsftime.c; strftime converts its
 * narrow format to wide, runs the core, then converts the wide result back to
 * multibyte (statefully) so the two functions share one %-directive table. */
size_t __wcsftime_core(wchar_t *, const wchar_t *, const struct tm *);

size_t
strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
	/* Wide conversion buffers (same render ceiling as the old strftime). */
	wchar_t wfmt[4096], wbuf[4096];
	size_t i = 0, wf = 0;

	/* Narrow format -> wide (byte-as-wchar ASCII conversion; the format's
	 * ASCII metacharacters and our handling are locale-independent). */
	for (i = 0; format[i] && wf < 4095; i++)
		wfmt[wf++] = (wchar_t)(unsigned char)format[i];
	wfmt[wf] = L'\0';

	size_t len = __wcsftime_core(wbuf, wfmt, tm);

	/* Wide result -> multibyte. All rendered fields are ASCII, so a plain
	 * byte-per-wchar fill is exact and avoids needing a locale state here. */
	if (len >= max) {
		if (max > 0) {
			for (i = 0; i < max - 1; i++)
				s[i] = (char)wbuf[i];
			s[max - 1] = '\0';
		}
		return 0;
	}
	for (i = 0; i < len; i++)
		s[i] = (char)wbuf[i];
	s[len] = '\0';
	return len;
}

double
difftime(time_t a, time_t b)
{
	return (double)(a - b);
}

/* C99 7.23.3.1: "Sun Jan  1 00:00:00 1900\n"-style fixed format. */
char *
asctime(const struct tm *tm)
{
	static char buf[26];
	if (strftime(buf, sizeof buf, "%a %b %e %H:%M:%S %Y\n", tm) == 0)
		return NULL;
	return buf;
}

char *
ctime(const time_t *t)
{
	struct tm *tmp = localtime(t);
	if (!tmp)
		return NULL;
	return asctime(tmp);
}

clock_t
clock(void)
{
	struct tms usage;
	if (times(&usage) == (clock_t)-1)
		return (clock_t)-1;
	return usage.tms_utime + usage.tms_stime;
}
