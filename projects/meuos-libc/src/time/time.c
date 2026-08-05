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

time_t
mktime(struct tm *tm)
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

size_t
strftime(char *s, size_t max, const char *format, const struct tm *tm)
{
	char buf[4096];
	size_t pos = 0;
	static const char *wday_names[] = {"Sunday","Monday","Tuesday","Wednesday",
		"Thursday","Friday","Saturday"};
	static const char *wday_abbr[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	static const char *mon_names[] = {"January","February","March","April","May","June",
		"July","August","September","October","November","December"};
	static const char *mon_abbr[] = {"Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec"};

	/* Append a snprintf result, counting only the bytes actually written
	 * into buf (snprintf's return value is the would-be length and can
	 * exceed the available space, which would drive pos past the end of
	 * buf and then overflow at the terminating NUL). */
#define STRFTIME_ADD(...) do { \
		int __r = snprintf(buf + pos, sizeof(buf) - pos, __VA_ARGS__); \
		int __avail = (int)(sizeof(buf) - 1 - pos); \
		pos += (size_t)(__r < __avail ? __r : __avail); \
	} while (0)

	for (const char *f = format; *f && pos < sizeof(buf) - 1; f++) {
		if (*f != '%') {
			buf[pos++] = *f;
			continue;
		}
		f++;
		int pad = 0; /* 0=none, 1=zero, 2=space */
		if (*f == '0') { pad = 1; f++; }
		else if (*f == '-') { pad = 0; f++; }
		else if (*f == '^') { f++; } /* uppercase — skip for now */

		switch (*f) {
		case 'Y': STRFTIME_ADD("%04d", tm->tm_year + 1900); break;
		case 'y': STRFTIME_ADD("%02d", tm->tm_year % 100); break;
		case 'C': STRFTIME_ADD("%02d", (tm->tm_year + 1900) / 100); break;
		case 'm': STRFTIME_ADD("%02d", tm->tm_mon + 1); break;
		case 'd': STRFTIME_ADD("%02d", tm->tm_mday); break;
		case 'e': STRFTIME_ADD("%2d", tm->tm_mday); break;
		case 'H': STRFTIME_ADD("%02d", tm->tm_hour); break;
		case 'I': {
			int h12 = tm->tm_hour % 12;
			if (h12 == 0) h12 = 12;
			STRFTIME_ADD("%02d", h12); break;
		}
		case 'M': STRFTIME_ADD("%02d", tm->tm_min); break;
		case 'S': STRFTIME_ADD("%02d", tm->tm_sec); break;
		case 'p': STRFTIME_ADD("%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
		case 'P': STRFTIME_ADD("%s", tm->tm_hour < 12 ? "am" : "pm"); break;
		case 'a': STRFTIME_ADD("%s", wday_abbr[tm->tm_wday]); break;
		case 'A': STRFTIME_ADD("%s", wday_names[tm->tm_wday]); break;
		case 'w': STRFTIME_ADD("%d", tm->tm_wday); break;
		case 'u': STRFTIME_ADD("%d", tm->tm_wday ? tm->tm_wday : 7); break;
		case 'b':
		case 'h': STRFTIME_ADD("%s", mon_abbr[tm->tm_mon]); break;
		case 'B': STRFTIME_ADD("%s", mon_names[tm->tm_mon]); break;
		case 'j': STRFTIME_ADD("%03d", tm->tm_yday + 1); break;
		case 'U': {
			int w = (tm->tm_yday + 7 - tm->tm_wday) / 7;
			STRFTIME_ADD("%02d", w); break;
		}
		case 'W': {
			int first_wday = (tm->tm_yday - tm->tm_wday + 7) % 7;
			int w = (tm->tm_yday + 7 - first_wday) / 7;
			STRFTIME_ADD("%02d", w); break;
		}
		case 'V': {
			/* ISO week number */
			int jan1_wday = (tm->tm_wday - tm->tm_yday % 7 + 7) % 7;
			int week = (tm->tm_yday + 7 - jan1_wday + 1) / 7;
			STRFTIME_ADD("%02d", week); break;
		}
		case 'G': STRFTIME_ADD("%04d", tm->tm_year + 1900); break;
		case 'g': STRFTIME_ADD("%02d", (tm->tm_year + 1900) % 100); break;
		case 'c': {
			/* %a %b %e %T %Y */
			STRFTIME_ADD("%s %s %2d %02d:%02d:%02d %04d",
				wday_abbr[tm->tm_wday], mon_abbr[tm->tm_mon], tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
			break;
		}
		case 'x': /* %m/%d/%y */
			STRFTIME_ADD("%02d/%02d/%02d",
				tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
			break;
		case 'X':
			STRFTIME_ADD("%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
			break;
		case 'D': /* %m/%d/%y */
			STRFTIME_ADD("%02d/%02d/%02d",
				tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
			break;
		case 'F': /* %Y-%m-%d */
			STRFTIME_ADD("%04d-%02d-%02d",
				tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
			break;
		case 'T': /* %H:%M:%S */
			STRFTIME_ADD("%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
			break;
		case 'r': /* %I:%M:%S %p */
			STRFTIME_ADD("%02d:%02d:%02d %s",
				tm->tm_hour % 12 ? tm->tm_hour % 12 : 12,
				tm->tm_min, tm->tm_sec,
				tm->tm_hour < 12 ? "AM" : "PM");
			break;
		case 'R': /* %H:%M */
			STRFTIME_ADD("%02d:%02d", tm->tm_hour, tm->tm_min);
			break;
		case 'z': /* timezone offset — not supported */
			break;
		case 'Z': /* timezone name — not supported */
			break;
		case '%':
			buf[pos++] = '%';
			break;
		case 'n':
			buf[pos++] = '\n';
			break;
		case 't':
			buf[pos++] = '\t';
			break;
		default:
			/* Unknown specifier: copy literally */
			buf[pos++] = '%';
			if (*f) buf[pos++] = *f;
			break;
		}
		if (pos >= sizeof(buf)) break;
	}
	if (pos >= sizeof(buf))
		pos = sizeof(buf) - 1;
	buf[pos] = '\0';
	if (pos >= max) {
		if (max > 0) { memcpy(s, buf, max - 1); s[max - 1] = '\0'; }
		return 0;
	}
	memcpy(s, buf, pos + 1);
	return pos;
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
