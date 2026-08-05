#ifndef _WCHAR_H
#define _WCHAR_H

#include <stddef.h>
#include <stdio.h>

/* wchar_t comes from <stddef.h> (C99 7.17). */
typedef int wint_t;

#define WEOF (-1)
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647

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

/* Wide character classification */
int iswalnum(wint_t);
int iswalpha(wint_t);
int iswcntrl(wint_t);
int iswdigit(wint_t);
int iswgraph(wint_t);
int iswlower(wint_t);
int iswprint(wint_t);
int iswpunct(wint_t);
int iswspace(wint_t);
int iswupper(wint_t);
int iswxdigit(wint_t);
wint_t towlower(wint_t);
wint_t towupper(wint_t);

/* Conversion */
size_t mbrtowc(wchar_t *, const char *, size_t, void *);
size_t wcrtomb(char *, wchar_t, void *);
size_t mbsrtowcs(wchar_t *, const char **, size_t, void *);
size_t wcsrtombs(char *, const wchar_t **, size_t, void *);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
wint_t btowc(int);
int wctob(wint_t);
size_t mbrlen(const char *, size_t, void *);
int wcwidth(wchar_t);

/* I/O */
wint_t fgetwc(FILE *);
wint_t fputwc(wchar_t, FILE *);
wint_t ungetwc(wint_t, FILE *);
int fwide(FILE *, int);
#endif
