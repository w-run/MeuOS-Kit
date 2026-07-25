#include <stddef.h>
#include <time.h>

/* Minimal stub: returns empty string to satisfy mcc __DATE__/__TIME__ macros. */
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    (void)format;
    (void)tm;
    if (max > 0) s[0] = '\0';
    return 0;
}

struct tm *localtime(const time_t *t) {
    (void)t;
    return NULL;
}
char *setlocale(int c, const char *l) { (void)c; (void)l; return "C"; }
char *mkdtemp(char *t) { (void)t; return NULL; }
