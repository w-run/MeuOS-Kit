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

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3

struct glob_t {
	size_t gl_pathc;
	char **gl_pathv;
	size_t gl_offs;
	/* internal */
	size_t capacity;
};

void globfree(struct glob_t *);
int glob(const char *restrict, int, int (*)(const char *, int), struct glob_t *restrict);

#endif
