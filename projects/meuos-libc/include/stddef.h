#ifndef MEUOS_STDDEF_H
#define MEUOS_STDDEF_H

/* Pointer-width scalar types. */
#if defined(__i386__)
typedef unsigned int size_t;
typedef int ptrdiff_t;
#else
typedef unsigned long size_t;
typedef long ptrdiff_t;
#endif

#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

/* C99 7.17: wchar_t is provided by <stddef.h>; <wchar.h> reuses it. */
typedef int wchar_t;

#endif
