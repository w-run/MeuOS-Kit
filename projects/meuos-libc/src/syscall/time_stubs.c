#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

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

/* mkdtemp: creates a temp directory from template (XXXXXX suffix).
 * Uses a PID-based approach instead of rand() since meuos-libc's
 * stdlib.h does not currently declare rand(). */
char *mkdtemp(char *t) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(t);
    if (len < 6) return NULL;
    char *suffix = t + len - 6;
    long pid = (long)getpid();
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (int i = 0; i < 6; ++i) {
            long idx = (pid + attempt * 13 + i * 7) & 0x1F;
            if ((size_t)idx >= sizeof(chars) - 1) idx = 0;
            suffix[i] = chars[(size_t)idx];
        }
        if (mkdir(t, 0700) == 0)
            return t;
    }
    return NULL;
}
