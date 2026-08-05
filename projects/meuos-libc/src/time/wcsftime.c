/* time/wcsftime.c — wide-character time formatting (C11 7.29.5.1).
 *
 * wcsftime() formats broken-down time (struct tm) per a format string into a
 * wide-character buffer. The formatting core __wcsftime_core() emits into a
 * wchar_t buffer and is shared with the narrow strftime() (see time.c), which
 * delegates to it to avoid maintaining two parallel %-directive tables.
 *
 * Name/month tables are kept as narrow ASCII literal tables (converted to
 * wchar_t per character when emitting) rather than wide string literals, so
 * this file carries no dependency on mcc wide-literal handling.
 */

#include <wchar.h>
#include <time.h>
#include <string.h>
#include <stddef.h>

/* Core: format `tm` per wide format string into out (caller must provide at
 * least BUFSIZ wide chars). Returns the number of wide characters written
 * (excluding the terminating NUL) and always NUL-terminates out. No buffer
 * size is taken here because callers render into a fixed large stack buffer
 * and apply their own maxsize bounds afterwards, mirroring the old strftime
 * buf[4096] approach. */
size_t
__wcsftime_core(wchar_t *out, const wchar_t *format, const struct tm *tm)
{
	const char *wday_names[] = {"Sunday","Monday","Tuesday","Wednesday",
		"Thursday","Friday","Saturday"};
	const char *wday_abbr[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	const char *mon_names[] = {"January","February","March","April","May","June",
		"July","August","September","October","November","December"};
	const char *mon_abbr[] = {"Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec"};

	size_t pos = 0;
#define WS_BUF 4096

	/* Emit a single wide char. */
#define WS_PUTC(c) do { \
		if (pos < WS_BUF - 1) out[pos++] = (c); \
	} while (0)

	/* Emit an ASCII narrow string, char-by-char as wide chars. */
#define WS_PUTS(s) do { \
		const char *_p = (s); \
		while (*_p) WS_PUTC((wchar_t)(unsigned char)*_p++); \
	} while (0)

	/* Emit a field as a padded decimal wide string.
	 * pad: 0 = no pad, 1 = zero pad, 2 = space pad. width = min digits. */
#define WS_PUTINT(val, width, pad) do { \
		int _v = (val); \
		int _neg = _v < 0; \
		unsigned _uv = _neg ? (unsigned)(-(_v + 1)) + 1u : (unsigned)_v; \
		char _dig[16]; int _nd = 0; \
		do { _dig[_nd++] = (char)('0' + _uv % 10); _uv /= 10; } while (_uv); \
		int _tot = _nd + (_neg ? 1 : 0); \
		int _z = (pad == 1) ? (width - _tot) : 0; \
		int _sp = (pad == 2) ? (width - _tot) : 0; \
		if (_neg && pad != 1) WS_PUTC(L'-'); \
		while (_sp > 0) { WS_PUTC(L' '); _sp--; } \
		while (_z > 0) { WS_PUTC(L'0'); _z--; } \
		if (_neg && pad == 1) WS_PUTC(L'-'); \
		while (_nd > 0) WS_PUTC((wchar_t)_dig[--_nd]); \
	} while (0)

	for (const wchar_t *f = format; *f && pos < WS_BUF - 1; f++) {
		if (*f != L'%') {
			WS_PUTC(*f);
			continue;
		}
		f++;
		int pad = 0; /* 0=none, 1=zero, 2=space */
		if (*f == L'0') { pad = 1; f++; }
		else if (*f == L'-') { pad = 0; f++; }
		else if (*f == L'^') { f++; } /* uppercase — skipped (as narrow) */

		switch (*f) {
		case L'Y': WS_PUTINT(tm->tm_year + 1900, 4, 1); break;
		case L'y': WS_PUTINT(tm->tm_year % 100, 2, 1); break;
		case L'C': WS_PUTINT((tm->tm_year + 1900) / 100, 2, 1); break;
		case L'm': WS_PUTINT(tm->tm_mon + 1, 2, 1); break;
		case L'd': WS_PUTINT(tm->tm_mday, 2, 1); break;
		case L'e': WS_PUTINT(tm->tm_mday, 2, 2); break;
		case L'H': WS_PUTINT(tm->tm_hour, 2, 1); break;
		case L'I': {
			int h12 = tm->tm_hour % 12;
			if (h12 == 0) h12 = 12;
			WS_PUTINT(h12, 2, 1); break;
		}
		case L'M': WS_PUTINT(tm->tm_min, 2, 1); break;
		case L'S': WS_PUTINT(tm->tm_sec, 2, 1); break;
		case L'p': WS_PUTS(tm->tm_hour < 12 ? "AM" : "PM"); break;
		case L'P': WS_PUTS(tm->tm_hour < 12 ? "am" : "pm"); break;
		case L'a': WS_PUTS(wday_abbr[tm->tm_wday]); break;
		case L'A': WS_PUTS(wday_names[tm->tm_wday]); break;
		case L'w': WS_PUTINT(tm->tm_wday, 0, 0); break;
		case L'u': WS_PUTINT(tm->tm_wday ? tm->tm_wday : 7, 0, 0); break;
		case L'b':
		case L'h': WS_PUTS(mon_abbr[tm->tm_mon]); break;
		case L'B': WS_PUTS(mon_names[tm->tm_mon]); break;
		case L'j': WS_PUTINT(tm->tm_yday + 1, 3, 1); break;
		case L'U': {
			int w = (tm->tm_yday + 7 - tm->tm_wday) / 7;
			WS_PUTINT(w, 2, 1); break;
		}
		case L'W': {
			int first_wday = (tm->tm_yday - tm->tm_wday + 7) % 7;
			int w = (tm->tm_yday + 7 - first_wday) / 7;
			WS_PUTINT(w, 2, 1); break;
		}
		case L'V': {
			/* ISO week number */
			int jan1_wday = (tm->tm_wday - tm->tm_yday % 7 + 7) % 7;
			int week = (tm->tm_yday + 7 - jan1_wday + 1) / 7;
			WS_PUTINT(week, 2, 1); break;
		}
		case L'G': WS_PUTINT(tm->tm_year + 1900, 4, 1); break;
		case L'g': WS_PUTINT((tm->tm_year + 1900) % 100, 2, 1); break;
		case L'c': {
			/* %a %b %e %T %Y */
			WS_PUTS(wday_abbr[tm->tm_wday]); WS_PUTC(L' ');
			WS_PUTS(mon_abbr[tm->tm_mon]); WS_PUTC(L' ');
			WS_PUTINT(tm->tm_mday, 2, 2); WS_PUTC(L' ');
			WS_PUTINT(tm->tm_hour, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_min, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_sec, 2, 1); WS_PUTC(L' ');
			WS_PUTINT(tm->tm_year + 1900, 4, 1);
			break;
		}
		case L'x': /* %m/%d/%y */
		case L'D':
			WS_PUTINT(tm->tm_mon + 1, 2, 1); WS_PUTC(L'/');
			WS_PUTINT(tm->tm_mday, 2, 1); WS_PUTC(L'/');
			WS_PUTINT(tm->tm_year % 100, 2, 1);
			break;
		case L'X':
		case L'T': /* %H:%M:%S */
			WS_PUTINT(tm->tm_hour, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_min, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_sec, 2, 1);
			break;
		case L'F': /* %Y-%m-%d */
			WS_PUTINT(tm->tm_year + 1900, 4, 1); WS_PUTC(L'-');
			WS_PUTINT(tm->tm_mon + 1, 2, 1); WS_PUTC(L'-');
			WS_PUTINT(tm->tm_mday, 2, 1);
			break;
		case L'r': /* %I:%M:%S %p */
			WS_PUTINT(tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, 2, 1);
			WS_PUTC(L':');
			WS_PUTINT(tm->tm_min, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_sec, 2, 1); WS_PUTC(L' ');
			WS_PUTS(tm->tm_hour < 12 ? "AM" : "PM");
			break;
		case L'R': /* %H:%M */
			WS_PUTINT(tm->tm_hour, 2, 1); WS_PUTC(L':');
			WS_PUTINT(tm->tm_min, 2, 1);
			break;
		case L'z': /* timezone offset — not supported */
		case L'Z': /* timezone name — not supported */
			break;
		case L'%': WS_PUTC(L'%'); break;
		case L'n': WS_PUTC(L'\n'); break;
		case L't': WS_PUTC(L'\t'); break;
		default:
			/* Unknown specifier: copy literally */
			WS_PUTC(L'%');
			if (*f) WS_PUTC(*f);
			break;
		}
		if (pos >= WS_BUF - 1) break;
	}
	out[pos] = L'\0';
	return pos;
}

size_t
wcsftime(wchar_t *wcs, size_t maxsize, const wchar_t *format, const struct tm *tm)
{
	if (maxsize == 0)
		return 0;
	wchar_t buf[4096];
	size_t len = __wcsftime_core(buf, format, tm);
	/* C11 7.29.5.1: if the total (incl. terminating NUL) exceeds maxsize,
	 * contents are indeterminate and 0 is returned. */
	if (len + 1 > maxsize)
		return 0;
	memcpy(wcs, buf, (len + 1) * sizeof(wchar_t));
	return len;
}
