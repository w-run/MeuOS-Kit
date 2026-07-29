#ifndef MEUOS_STDIO_EXT_H
#define MEUOS_STDIO_EXT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* glibc extension: stdio internal state queries.  Most real-world software
 * only uses __fsetlocking which we stub as a no-op. */

#define FSETLOCKING_QUERY 0
#define FSETLOCKING_INTERNAL 1
#define FSETLOCKING_BYCALLER 2

static inline int __fsetlocking(FILE *f, int type) { (void)f; (void)type; return FSETLOCKING_BYCALLER; }
static inline int __freading(FILE *f) { (void)f; return 0; }
static inline int __fwriting(FILE *f) { (void)f; return 0; }
static inline int __fbufsize(FILE *f) { (void)f; return 0; }
static inline void __fpurge(FILE *f) { (void)f; }
static inline void __fpending(FILE *f) { (void)f; }
static inline int _IO_ferror(FILE *f) { return ferror(f); }

#ifdef __cplusplus
}
#endif

#endif
