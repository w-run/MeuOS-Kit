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
