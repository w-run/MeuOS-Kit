#ifndef _WCTYPE_H
#define _WCTYPE_H

#include <stddef.h>
#include <wchar.h>

/* wctype_t describes a character class (an opaque handle to a property). */
typedef unsigned long wctype_t;

/* wctrans_t describes a character mapping (tolower / toupper). */
typedef unsigned long wctrans_t;

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

/* 7.29.2.2: iswctype — test a wide character against a class descriptor. */
int iswctype(wint_t, wctype_t);

/* 7.29.2.2: wctype — obtain a class descriptor by name. */
wctype_t wctype(const char *);

/* 7.29.6.4: towctrans — apply a mapping described by a descriptor. */
wint_t towctrans(wint_t, wctrans_t);

/* 7.29.6.4: wctrans — obtain a mapping descriptor by name. */
wctrans_t wctrans(const char *);

#endif
