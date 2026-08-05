#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdio.h>

/* wchar_t comes from <stddef.h> (C99 7.17). */
typedef int wint_t;

#define WEOF (-1)
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647

/* Multibyte conversion state (C11 7.29.1).  Internal layout: __left is the
 * number of bytes still expected to finish the in-progress sequence (0 means
 * idle), __acc accumulates the decoded code point, __nbits tracks the bit
 * width of the pending lead byte.  Only UTF-8 is decoded; the state is small,
 * trivially copyable, and mbsinit() reports idle when __left == 0. */
typedef struct {
	unsigned __left;
	unsigned __acc;
	unsigned __nbits;
} mbstate_t;

int mbsinit(const mbstate_t *);

/* String operations */
wchar_t *wcscpy(wchar_t *, const wchar_t *);
wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
wchar_t *wcscat(wchar_t *, const wchar_t *);
wchar_t *wcsncat(wchar_t *, const wchar_t *, size_t);
int wcscmp(const wchar_t *, const wchar_t *);
int wcsncmp(const wchar_t *, const wchar_t *, size_t);
wchar_t *wcschr(const wchar_t *, wchar_t);
wchar_t *wcsrchr(const wchar_t *, wchar_t);
size_t wcscspn(const wchar_t *, const wchar_t *);
size_t wcsspn(const wchar_t *, const wchar_t *);
wchar_t *wcspbrk(const wchar_t *, const wchar_t *);
wchar_t *wcsstr(const wchar_t *, const wchar_t *);
size_t wcslen(const wchar_t *);
wchar_t *wcstok(wchar_t *, const wchar_t *, wchar_t **);

/* Wide character classification and mapping (C11 7.29.2 / 7.29.6.4) */
#include <wctype.h>

/* Conversion */
size_t mbrtowc(wchar_t *, const char *, size_t, mbstate_t *);
size_t wcrtomb(char *, wchar_t, mbstate_t *);
size_t mbsrtowcs(wchar_t *, const char **, size_t, mbstate_t *);
size_t wcsrtombs(char *, const wchar_t **, size_t, mbstate_t *);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
wint_t btowc(int);
int wctob(wint_t);
size_t mbrlen(const char *, size_t, mbstate_t *);
int wcwidth(wchar_t);

/* I/O */
wint_t fgetwc(FILE *);
wint_t fputwc(wchar_t, FILE *);
wint_t ungetwc(wint_t, FILE *);
int fwide(FILE *, int);
wchar_t *fgetws(wchar_t *, int, FILE *);
wint_t fputws(const wchar_t *, FILE *);
wint_t getwchar(void);
wint_t putwchar(wchar_t);
#endif
