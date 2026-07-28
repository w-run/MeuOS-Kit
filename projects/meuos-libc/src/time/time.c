/* time/time.c — POSIX time functions */

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
	int y, m, d, h, min, s, wday, yday;

	/* Break down seconds */
	s = (int)(secs % 60); secs /= 60;
	min = (int)(secs % 60); secs /= 60;
	h = (int)(secs % 24); secs /= 24;

	/* days since epoch */
	long long days = secs;

	/* Year */
	y = 1970;
	while (1) {
		int leap = is_leap(y);
		int ydays = leap ? 366 : 365;
		if (days < ydays) break;
		days -= ydays;
		y++;
	}

	/* Month */
	int leap = is_leap(y);
	for (m = 0; m < 12; m++) {
		if (days < days_in_mon[leap][m]) break;
		days -= days_in_mon[leap][m];
	}
	d = (int)days + 1; /* 1-based */

	/* Day of week: 1970-01-01 was Thursday (4) */
	wday = (int)(((long long)*t / 86400 + 4) % 7);
	if (wday < 0) wday += 7;

	/* Day of year */
	yday = 0;
	for (int i = 0; i < m; i++) yday += days_in_mon[leap][i];
	yday += d - 1;

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
		case 'Y': pos += snprintf(buf+pos, sizeof(buf)-pos, "%04d", tm->tm_year + 1900); break;
		case 'y': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_year % 100); break;
		case 'C': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", (tm->tm_year + 1900) / 100); break;
		case 'm': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_mon + 1); break;
		case 'd': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_mday); break;
		case 'e': pos += snprintf(buf+pos, sizeof(buf)-pos, "%2d", tm->tm_mday); break;
		case 'H': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_hour); break;
		case 'I': {
			int h12 = tm->tm_hour % 12;
			if (h12 == 0) h12 = 12;
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", h12); break;
		}
		case 'M': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_min); break;
		case 'S': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", tm->tm_sec); break;
		case 'p': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
		case 'P': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", tm->tm_hour < 12 ? "am" : "pm"); break;
		case 'a': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", wday_abbr[tm->tm_wday]); break;
		case 'A': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", wday_names[tm->tm_wday]); break;
		case 'w': pos += snprintf(buf+pos, sizeof(buf)-pos, "%d", tm->tm_wday); break;
		case 'u': pos += snprintf(buf+pos, sizeof(buf)-pos, "%d", tm->tm_wday ? tm->tm_wday : 7); break;
		case 'b':
		case 'h': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", mon_abbr[tm->tm_mon]); break;
		case 'B': pos += snprintf(buf+pos, sizeof(buf)-pos, "%s", mon_names[tm->tm_mon]); break;
		case 'j': pos += snprintf(buf+pos, sizeof(buf)-pos, "%03d", tm->tm_yday + 1); break;
		case 'U': {
			int w = (tm->tm_yday + 7 - tm->tm_wday) / 7;
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", w); break;
		}
		case 'W': {
			int first_wday = (tm->tm_yday - tm->tm_wday + 7) % 7;
			int w = (tm->tm_yday + 7 - first_wday) / 7;
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", w); break;
		}
		case 'V': {
			/* ISO week number */
			int jan1_wday = (tm->tm_wday - tm->tm_yday % 7 + 7) % 7;
			int week = (tm->tm_yday + 7 - jan1_wday + 1) / 7;
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", week); break;
		}
		case 'G': pos += snprintf(buf+pos, sizeof(buf)-pos, "%04d", tm->tm_year + 1900); break;
		case 'g': pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d", (tm->tm_year + 1900) % 100); break;
		case 'c': {
			/* %a %b %e %T %Y */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%s %s %2d %02d:%02d:%02d %04d",
				wday_abbr[tm->tm_wday], mon_abbr[tm->tm_mon], tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
			break;
		}
		case 'x': /* %m/%d/%y */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d/%02d/%02d",
				tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
			break;
		case 'X':
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
			break;
		case 'D': /* %m/%d/%y */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d/%02d/%02d",
				tm->tm_mon+1, tm->tm_mday, tm->tm_year % 100);
			break;
		case 'F': /* %Y-%m-%d */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%04d-%02d-%02d",
				tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
			break;
		case 'T': /* %H:%M:%S */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d:%02d:%02d",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
			break;
		case 'r': /* %I:%M:%S %p */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d:%02d:%02d %s",
				tm->tm_hour % 12 ? tm->tm_hour % 12 : 12,
				tm->tm_min, tm->tm_sec,
				tm->tm_hour < 12 ? "AM" : "PM");
			break;
		case 'R': /* %H:%M */
			pos += snprintf(buf+pos, sizeof(buf)-pos, "%02d:%02d", tm->tm_hour, tm->tm_min);
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
	buf[pos] = '\0';
	if (pos >= max) {
		if (max > 0) { memcpy(s, buf, max - 1); s[max - 1] = '\0'; }
		return 0;
	}
	memcpy(s, buf, pos + 1);
	return pos;
}
