#ifndef _GLOB_H
#define _GLOB_H

#include <sys/types.h>

#define GLOB_ERR      1
#define GLOB_MARK     2
#define GLOB_NOSORT   4
#define GLOB_DOOFFS   8
#define GLOB_NOCHECK  16
#define GLOB_APPEND   32
#define GLOB_NOESCAPE 64
#define GLOB_PERIOD   128

/* glibc/musl 兼容标志位（本实现仅使用其中子集，未实现的功能被安全忽略） */
#define GLOB_MAGCHAR  0x100
#define GLOB_ALTDIRFUNC 0x200
#define GLOB_TILDE    0x400
#define GLOB_TILDE_CHECK 0x800
#define GLOB_BRACE    0x1000
#define GLOB_NOMAGIC  0x2000

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3

typedef struct glob_t {
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
	/* internal */
	size_t capacity;
} glob_t;

void globfree(struct glob_t *);
int glob(const char *restrict, int, int (*)(const char *, int), struct glob_t *restrict);

#endif
