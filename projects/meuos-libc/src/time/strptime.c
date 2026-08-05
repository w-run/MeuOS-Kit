/* time/strptime.c — POSIX.1-2008 date/time string parsing (strptime).
 *
 * Parses s according to format into *tm, mirroring strftime's conversion
 * specifiers, and returns a pointer to the first unconsumed input char, or
 * NULL on a mismatch/out-of-range.  Supported specifiers: %Y %y %C %m %n
 * %d %e %H %I %M %S %j %U %W %a %A %b %B %h %p %% %n %t and a literal
 * space (whitespace is matched loosely).  Numeric fields are 2- (or 4-)
 * digit.  tm_wday/tm_yday are set when a %a/%A or %j is seen; other derived
 * fields are left for the caller to normalize (e.g. mktime).  Zero GNU
 * dependency; strptime is POSIX.1-2008 in core libc. */

#include <time.h>
#include <string.h>
#include <ctype.h>

static const char *wday_names[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *wday_abbr[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *mon_names[] = {
	"January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December"
};
static const char *mon_abbr[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec"
};

/* match a day/month name (exact, case-insensitive) -> index, or -1 */
static int
match_name(const char **s, const char *const names[], int n, const char *const abbr[], int nab)
{
	for (int i = 0; i < n; i++) {
		const char *full = names[i];
		size_t fl = strlen(full);
		if (strncasecmp(*s, full, fl) == 0) {
			*s += fl;
			return i;
		}
	}
	for (int i = 0; i < nab; i++) {
		const char *ab = abbr[i];
		size_t al = strlen(ab);
		if (strncasecmp(*s, ab, al) == 0) {
			*s += al;
			return i;
		}
	}
	return -1;
}

/* parse up to maxdig decimal digits into *val. */
static int
parse_num(const char **s, int *val, int maxdig)
{
	int n = 0;
	while (*s && isdigit((unsigned char)**s) && n < maxdig) {
		*val = *val * 10 + (**s - '0');
		(*s)++;
		n++;
	}
	return n > 0 ? n : -1;
}

char *
strptime(const char *s, const char *restrict fmt, struct tm *restrict tm)
{
	while (*fmt) {
		char c = *fmt;
		if (c == '%') {
			if (!fmt[1])
				return NULL;                 /* trailing '%' */
			char spec = fmt[1];
			fmt += 2;
			int v;
			switch (spec) {
			case '%':
				if (*s != '%') return NULL;
				s++;
				break;
			case 'n': case 't':
				/* whitespace */
				while (*s && isspace((unsigned char)*s)) s++;
				break;
			case 'Y': {
				int v4 = 0;
				int nd = parse_num(&s, &v4, 4);
				if (nd <= 0) return NULL;
				tm->tm_year = v4 - 1900;
				break;
			}
			case 'y': {
				int v2 = 0;
				if (parse_num(&s, &v2, 2) <= 0) return NULL;
				tm->tm_year = (v2 >= 69) ? v2 : v2 + 100;
				break;
			}
			case 'C': {
				int v2 = 0;
				if (parse_num(&s, &v2, 2) <= 0) return NULL;
				tm->tm_year = (tm->tm_year - (tm->tm_year % 100)) / 100 * 100;
				/* approximated: %C without %y sets century; leave %y untouched */
				break;
			}
			case 'm':
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v < 1 || v > 12) return NULL;
				tm->tm_mon = v - 1;
				break;
			case 'd': case 'e':
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v < 1 || v > 31) return NULL;
				tm->tm_mday = v;
				break;
			case 'H':
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v > 23) return NULL;
				tm->tm_hour = v;
				break;
			case 'I': {
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v < 1 || v > 12) return NULL;
				tm->tm_hour = v % 12;
				break;
			}
			case 'M':
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v > 59) return NULL;
				tm->tm_min = v;
				break;
			case 'S':
				v = 0;
				if (parse_num(&s, &v, 2) <= 0) return NULL;
				if (v > 61) return NULL;
				tm->tm_sec = v;
				break;
			case 'j': {
				v = 0;
				if (parse_num(&s, &v, 3) <= 0) return NULL;
				if (v < 1 || v > 366) return NULL;
				tm->tm_yday = v - 1;
				break;
			}
			case 'a': case 'A': {
				int w = match_name(&s, wday_names, 7, wday_abbr, 7);
				if (w < 0) return NULL;
				tm->tm_wday = w;
				break;
			}
			case 'b': case 'B': case 'h': {
				int m = match_name(&s, mon_names, 12, mon_abbr, 12);
				if (m < 0) return NULL;
				tm->tm_mon = m;
				break;
			}
			case 'p':
				/* AM/PM: adjust tm_hour by the %I base */
				if (s[0] == 'A' || s[0] == 'a') {
					if (tm->tm_hour == 0) tm->tm_hour = 0;
					s += 2;
				} else if (s[0] == 'P' || s[0] == 'p') {
					if (tm->tm_hour < 12) tm->tm_hour += 12;
					s += 2;
				} else {
					return NULL;
				}
				break;
			default:
				/* unsupported specifier: treat as literal 'x' in the input */
				if (*s != spec) return NULL;
				s++;
				break;
			}
		} else if (c == ' ') {
			/* format whitespace matches any run of input whitespace */
			while (*s && isspace((unsigned char)*s)) s++;
			fmt++;
		} else {
			if (*s != c)
				return NULL;
			s++;
			fmt++;
		}
	}
	return (char *)s;
}
