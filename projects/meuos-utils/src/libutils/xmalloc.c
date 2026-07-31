/* libutils/xmalloc.c — OOM 时 die() 的分配器 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

void *xmalloc(size_t n) {
    if (n == 0) n = 1;  /* malloc(0) 实现差异，避免返回 NULL 触发误判 */
    void *p = malloc(n);
    if (!p) die("xmalloc: out of memory (request %zu bytes)", n);
    return p;
}

void *xcalloc(size_t n, size_t s) {
    if (n == 0 || s == 0) { n = 1; s = 1; }
    /* 防溢出 */
    if (n > SIZE_MAX / s) die("xcalloc: size overflow (%zu * %zu)", n, s);
    void *p = calloc(n, s);
    if (!p) die("xcalloc: out of memory (request %zu bytes)", n * s);
    return p;
}

void *xrealloc(void *p, size_t n) {
    if (n == 0) n = 1;
    void *q = realloc(p, n);
    if (!q) die("xrealloc: out of memory (request %zu bytes)", n);
    return q;
}

char *xstrdup(const char *s) {
    if (!s) die("xstrdup: NULL argument");
    size_t n = strlen(s) + 1;
    char *r = xmalloc(n);
    memcpy(r, s, n);
    return r;
}

char *xstrndup(const char *s, size_t n) {
    if (!s) die("xstrndup: NULL argument");
    size_t actual = 0;
    while (actual < n && s[actual]) actual++;
    char *r = xmalloc(actual + 1);
    memcpy(r, s, actual);
    r[actual] = '\0';
    return r;
}
