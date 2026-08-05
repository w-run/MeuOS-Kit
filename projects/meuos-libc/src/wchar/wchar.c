/* wchar/wchar.c — Wide character functions */

#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* String operations */
size_t wcslen(const wchar_t *s) {
	size_t n = 0; while (*s++) n++; return n;
}

wchar_t *wcscpy(wchar_t *d, const wchar_t *s) {
	wchar_t *r = d; while ((*d++ = *s++)); return r;
}

wchar_t *wcsncpy(wchar_t *d, const wchar_t *s, size_t n) {
	wchar_t *r = d; while (n-- && (*d++ = *s++));
	while (n-- > 0) *d++ = 0; return r;
}

wchar_t *wcscat(wchar_t *d, const wchar_t *s) {
	wcscpy(d + wcslen(d), s); return d;
}

wchar_t *wcsncat(wchar_t *d, const wchar_t *s, size_t n) {
	wchar_t *r = d; d += wcslen(d);
	while (n-- && *s) *d++ = *s++;
	*d = 0; return r;
}

int wcscmp(const wchar_t *a, const wchar_t *b) {
	while (*a && *a == *b) { a++; b++; }
	return (int)(*a - *b);
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) {
	if (!n) return 0;
	while (--n && *a && *a == *b) { a++; b++; }
	return (int)(*a - *b);
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
	while (*s && *s != c) s++;
	return *s == c ? (wchar_t *)s : NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
	const wchar_t *p = NULL;
	while (*s) { if (*s == c) p = s; s++; }
	return (wchar_t *)p;
}

size_t wcscspn(const wchar_t *s, const wchar_t *r) {
	size_t n = 0;
	for (; *s; s++, n++)
		for (const wchar_t *p = r; *p; p++)
			if (*s == *p) return n;
	return n;
}

size_t wcsspn(const wchar_t *s, const wchar_t *a) {
	size_t n = 0;
	for (; *s; s++, n++) {
		int found = 0;
		for (const wchar_t *p = a; *p; p++)
			if (*s == *p) { found = 1; break; }
		if (!found) break;
	}
	return n;
}

wchar_t *wcspbrk(const wchar_t *s, const wchar_t *a) {
	while (*s) {
		for (const wchar_t *p = a; *p; p++)
			if (*s == *p) return (wchar_t *)s;
		s++;
	}
	return NULL;
}

wchar_t *wcsstr(const wchar_t *h, const wchar_t *n) {
	if (!*n) return (wchar_t *)h;
	for (; *h; h++) {
		const wchar_t *a = h, *b = n;
		while (*b && *a == *b) { a++; b++; }
		if (!*b) return (wchar_t *)h;
	}
	return NULL;
}

/* Character classification */
int iswalnum(wint_t c) { return isalnum((int)c); }
int iswalpha(wint_t c) { return isalpha((int)c); }
int iswcntrl(wint_t c) { return iscntrl((int)c); }
int iswdigit(wint_t c) { return isdigit((int)c); }
int iswgraph(wint_t c) { return isgraph((int)c); }
int iswlower(wint_t c) { return islower((int)c); }
int iswprint(wint_t c) { return isprint((int)c); }
int iswpunct(wint_t c) { return ispunct((int)c); }
int iswspace(wint_t c) { return isspace((int)c); }
int iswupper(wint_t c) { return isupper((int)c); }
int iswxdigit(wint_t c) { return isxdigit((int)c); }
wint_t towlower(wint_t c) { return tolower((int)c); }
wint_t towupper(wint_t c) { return toupper((int)c); }

/* MB/C conversion (simple: only ASCII, single-byte) */
static int mb_cur_max = 1;

int mblen(const char *s, size_t n) {
	if (!s) return 0; /* no state-dependent encodings */
	if (n == 0 || !*s) return 0;
	return 1;
}

int mbtowc(wchar_t *pwc, const char *s, size_t n) {
	if (!s) return 0; /* no state-dependent encodings */
	if (n == 0 || !*s) return 0;
	if (pwc) *pwc = (unsigned char)*s;
	return (*s != '\0') ? 1 : 0;
}

int wctomb(char *s, wchar_t wc) {
	if (!s) return 0;
	if (wc > 0 && wc < 256) { *s = (char)(wc & 0xff); return 1; }
	*s = '?'; return 1;
}

size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n) {
	size_t i;
	for (i = 0; i < n && *s; i++) pwcs[i] = (unsigned char)*s++;
	if (i < n) pwcs[i] = 0;
	return i;
}

size_t wcstombs(char *s, const wchar_t *pwcs, size_t n) {
	size_t i;
	for (i = 0; i < n && *pwcs; i++) s[i] = (char)*pwcs++;
	if (i < n) s[i] = 0;
	return i;
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps) {
	(void)ps; return mbtowc(pwc, s, n);
}

size_t wcrtomb(char *s, wchar_t wc, void *ps) {
	(void)ps; return (size_t)wctomb(s, wc);
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t n, void *ps) {
	(void)ps;
	size_t r = mbstowcs(dst, *src, n);
	*src += r;
	return r;
}

size_t wcsrtombs(char *dst, const wchar_t **src, size_t n, void *ps) {
	(void)ps;
	size_t r = wcstombs(dst, *src, n);
	*src += r;
	return r;
}

/* C11 7.29.6.1.1: btowc — single byte to wide char. */
wint_t btowc(int c) {
	return (c == EOF || c < 0 || c > 255) ? WEOF : (wint_t)(unsigned char)c;
}

/* C11 7.29.6.1.2: wctob — wide char to single byte; EOF if not single-byte. */
int wctob(wint_t wc) {
	return (wc < 0 || wc > 255) ? EOF : (int)wc;
}

/* C11 7.29.6.3: mbrlen — size of the byte sequence for a multibyte char. */
size_t mbrlen(const char *s, size_t n, void *ps) {
	(void)ps;
	return (size_t)mblen(s, n);
}

/* wcwidth — printable column width (Unicode 15.0 EA Width + combining).
 *
 * The C locale still honours the established Unicode width contract:
 *   - -1 for invalid code points
 *   -  0 for control characters and zero-width (combining / format) marks
 *   -  2 for East Asian Wide (W) and Fullwidth (F) code points
 *   -  1 for everything else printable
 *
 * Data is encoded as sorted, half-open [lo,hi) ranges and searched with
 * bsearch.  Two tables: zero-width (combining/format) and wide (W/F).
 * Anything not listed and not a control char defaults to 1. */

/* Zero-width ranges: Mn (non-spacing mark), Me (enclosing mark), and the
 * format characters (Cf) that have no visible advance. */
struct wcr { unsigned lo, hi; };
static const struct wcr zw_ranges[] = {
	{0x0300,0x0370},{0x0483,0x048A},{0x0591,0x05BD},{0x05BF,0x05BF},
	{0x05C1,0x05C2},{0x05C4,0x05C5},{0x05C7,0x05C8},{0x0610,0x061B},
	{0x061C,0x061D},{0x064B,0x0660},{0x0670,0x0671},{0x06D6,0x06DD},
	{0x0711,0x0712},{0x0730,0x074A},{0x07A6,0x07B1},{0x07EB,0x07F4},
	{0x07F6,0x07FB},{0x07FD,0x07FE},{0x0816,0x0820},{0x0824,0x0829},
	{0x082D,0x082E},{0x0859,0x085E},{0x08E3,0x0904},{0x093A,0x093C},
	{0x093E,0x094F},{0x0951,0x0958},{0x0962,0x0964},{0x0981,0x0984},
	{0x09BC,0x09CD},{0x09D7,0x09D8},{0x09E2,0x09E4},{0x09FE,0x0A04},
	{0x0A3C,0x0A4E},{0x0A51,0x0A52},{0x0A70,0x0A72},{0x0A75,0x0A76},
	{0x0A81,0x0A84},{0x0ABC,0x0ACD},{0x0AC1,0x0AC6},{0x0ACA,0x0ACE},
	{0x0AE2,0x0AE4},{0x0AFA,0x0B04},{0x0B3C,0x0B3E},{0x0B47,0x0B49},
	{0x0B4B,0x0B4E},{0x0B56,0x0B58},{0x0B82,0x0B84},{0x0BC0,0x0BCD},
	{0x0BD7,0x0BD8},{0x0C00,0x0C04},{0x0C3E,0x0C57},{0x0C62,0x0C64},
	{0x0C81,0x0C84},{0x0CBC,0x0CCD},{0x0CBF,0x0CCC},{0x0CC2,0x0CCD},
	{0x0CD5,0x0CD7},{0x0CE2,0x0CE4},{0x0D00,0x0D04},{0x0D3B,0x0D3E},
	{0x0D4D,0x0D4F},{0x0D62,0x0D64},{0x0D82,0x0D84},{0x0DCA,0x0DCB},
	{0x0DCF,0x0DDF},{0x0DF2,0x0DF4},{0x0E31,0x0E33},{0x0E34,0x0E3B},
	{0x0E47,0x0E4F},{0x0EB1,0x0EB2},{0x0EB4,0x0EBB},{0x0EC8,0x0ECF},
	{0x0F18,0x0F1A},{0x0F35,0x0F36},{0x0F37,0x0F38},{0x0F3E,0x0F40},
	{0x0F71,0x0F88},{0x0F8D,0x0F99},{0x0F90,0x0FBD},{0x0FC6,0x0FC7},
	{0x0FCF,0x0FD0},{0x102D,0x1031},{0x1032,0x1039},{0x1039,0x103B},
	{0x103D,0x103F},{0x1056,0x105A},{0x105E,0x1061},{0x1071,0x1075},
	{0x1082,0x1084},{0x1085,0x1087},{0x108D,0x108E},{0x109D,0x109E},
	{0x135D,0x1360},{0x1390,0x139A},{0x1712,0x1715},{0x1732,0x1735},
	{0x1752,0x1754},{0x1772,0x1774},{0x17B4,0x17D4},{0x17DD,0x17DE},
	{0x18A9,0x18AA},{0x1920,0x192B},{0x1930,0x193B},{0x1A17,0x1A1C},
	{0x1A55,0x1A5F},{0x1A60,0x1A62},{0x1A65,0x1A6D},{0x1A6E,0x1A73},
	{0x1A7F,0x1A80},{0x1AB0,0x1ACE},{0x1B00,0x1B04},{0x1B34,0x1B45},
	{0x1B6B,0x1B74},{0x1B80,0x1B83},{0x1BA1,0x1BAB},{0x1BAC,0x1BAE},
	{0x1BE6,0x1BF2},{0x1C24,0x1C38},{0x1CAB,0x1CAD},{0x1CD0,0x1CE9},
	{0x1CED,0x1CEE},{0x1CF4,0x1CF5},{0x1CF7,0x1CF8},{0x1DC0,0x1DE7},
	{0x1DFB,0x1DFC},{0x1E00,0x1E02},{0x200B,0x200F},{0x202A,0x202F},
	{0x2060,0x2065},{0x2066,0x206F},	{0x20D0,0x20F1},{0x2CEF,0x2CF2},
	{0x2D7F,0x2D80},{0x2DE0,0x2DFF},{0x302A,0x3030},{0x3099,0x309B},
	{0x0A01,0x0A03},{0xFE00,0xFE0F},{0xFE20,0xFE2F},{0xFFF9,0xFFFC},
};

/* Wide (width 2) ranges: East Asian Width W and F. */
static const struct wcr wide_ranges[] = {
	{0x1100,0x1160},{0x11A8,0x11FF},{0x2E80,0x2EFF},{0x2F00,0x2FDF},
	{0x2FF0,0x2FFF},{0x3000,0x3040},{0x309B,0x30FF},{0x3105,0x312F},
	{0x3131,0x318F},{0x3190,0x31BF},{0x31C0,0x31EF},{0x31F0,0x31FF},
	{0x3200,0x32FF},{0x3300,0x33FF},{0x3400,0x4DBF},{0x4E00,0x9FFF},
	{0xA000,0xA4CF},{0xA4D0,0xA4FF},{0xA800,0xA82F},{0xA840,0xA87F},
	{0xA880,0xA8DF},{0xA8E0,0xA8FF},{0xA900,0xA92F},{0xA930,0xA95F},
	{0xA960,0xA97F},{0xAC00,0xD7A3},{0xD7B0,0xD7FF},{0xF900,0xFAFF},
	{0xFE10,0xFE1F},{0xFE30,0xFE4F},{0xFF00,0xFF60},{0xFFE0,0xFFE6},
	{0x1B000,0x1B12F},{0x1B170,0x1B2FF},{0x1F200,0x1F2FF},{0x1F300,0x1F5FF},
	{0x1F600,0x1F64F},{0x1F900,0x1FAFF},{0x20000,0x2FFFD},{0x30000,0x3FFFD},
};

static int in_ranges(const struct wcr *r, size_t n, unsigned c)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (c < r[mid].lo) hi = mid;
		else if (c >= r[mid].hi) lo = mid + 1;
		else return 1;
	}
	return 0;
}

int wcwidth(wchar_t c) {
	unsigned u = (unsigned)c;
	if (c < 0 || u > 0x10FFFF)
		return -1;
	/* C0 / C1 control area */
	if (u < 0x20 || (u >= 0x7f && u < 0xa0))
		return 0;
	if (in_ranges(zw_ranges, sizeof zw_ranges / sizeof *zw_ranges, u))
		return 0;
	if (in_ranges(wide_ranges, sizeof wide_ranges / sizeof *wide_ranges, u))
		return 2;
	return 1;
}

/* Wide I/O (simple wrappers around byte I/O) */
wint_t fgetwc(FILE *f) {
	int c = fgetc(f);
	return (c == EOF) ? WEOF : (wint_t)(unsigned char)c;
}

wint_t fputwc(wchar_t wc, FILE *f) {
	int c = fputc((int)(wc & 0xff), f);
	return (c == EOF) ? WEOF : (wint_t)(unsigned char)c;
}

wint_t ungetwc(wint_t wc, FILE *f) {
	return ungetc((int)(wc & 0xff), f);
}

int fwide(FILE *f, int mode) {
	(void)f; (void)mode; return 0; /* byte-oriented by default */
}

/* ----------------------------------------------------------------------
 * wctype / iswctype / wctrans / towctrans  (C11 7.29.2.2 / 7.29.6.4)
 *
 * The C locale is single-byte ASCII-only, so the class descriptors are
 * simply the corresponding <ctype.h> predicate bits, and the mapping
 * descriptors are 0 (tolower) or 1 (toupper).  Property names follow
 * C11 7.29.2.2.1; unknown names yield a descriptor of 0 (iswctype treats
 * 0 as "no class" -> always false, as required).
 * -------------------------------------------------------------------- */

#include <wctype.h>

/* Class descriptor encoding: bit per <ctype.h> predicate.  Keep in sync
 * with the mask values used by iswctype below. */
#define WC_ALPHA  0x01
#define WC_UPPER  0x02
#define WC_LOWER  0x04
#define WC_DIGIT  0x08
#define WC_XDIGIT 0x10
#define WC_SPACE  0x20
#define WC_PRINT  0x40
#define WC_GRAPH  0x80
#define WC_PUNCT  0x100
#define WC_CNTRL  0x200
#define WC_ALNUM  (WC_ALPHA | WC_DIGIT)
#define WC_BLANK  0x400

wctype_t wctype(const char *property) {
	if (!property) return 0;
	/* C11 7.29.2.2.1: the following names are recognised. */
	if (!strcmp(property, "alnum"))  return WC_ALNUM;
	if (!strcmp(property, "alpha"))  return WC_ALPHA;
	if (!strcmp(property, "blank"))  return WC_BLANK;
	if (!strcmp(property, "cntrl"))  return WC_CNTRL;
	if (!strcmp(property, "digit"))  return WC_DIGIT;
	if (!strcmp(property, "graph"))  return WC_GRAPH;
	if (!strcmp(property, "lower"))  return WC_LOWER;
	if (!strcmp(property, "print"))  return WC_PRINT;
	if (!strcmp(property, "punct"))  return WC_PUNCT;
	if (!strcmp(property, "space"))  return WC_SPACE;
	if (!strcmp(property, "upper"))  return WC_UPPER;
	if (!strcmp(property, "xdigit")) return WC_XDIGIT;
	return 0; /* unknown property */
}

int iswctype(wint_t wc, wctype_t desc) {
	if (!desc) return 0;
	if (wc < 0 || wc > 255) return 0; /* C locale: only ASCII */
	int c = (int)wc;
	if (desc & WC_ALPHA  && !isalpha(c)) return 0;
	if (desc & WC_UPPER  && !isupper(c)) return 0;
	if (desc & WC_LOWER  && !islower(c)) return 0;
	if (desc & WC_DIGIT  && !isdigit(c)) return 0;
	if (desc & WC_XDIGIT && !isxdigit(c)) return 0;
	if (desc & WC_SPACE  && !isspace(c)) return 0;
	if (desc & WC_PRINT  && !isprint(c)) return 0;
	if (desc & WC_GRAPH  && !isgraph(c)) return 0;
	if (desc & WC_PUNCT  && !ispunct(c)) return 0;
	if (desc & WC_CNTRL  && !iscntrl(c)) return 0;
	if (desc & WC_BLANK  && c != ' ' && c != '\t') return 0;
	return 1;
}

wctrans_t wctrans(const char *property) {
	if (!property) return 0;
	if (!strcmp(property, "tolower")) return 0;
	if (!strcmp(property, "toupper")) return 1;
	return 0; /* unknown mapping */
}

wint_t towctrans(wint_t wc, wctrans_t desc) {
	if (desc == 1) return towupper(wc);
	if (desc == 0) return towlower(wc);
	return wc; /* unknown -> unchanged */
}

