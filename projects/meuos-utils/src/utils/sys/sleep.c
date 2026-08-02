/* sleep — 暂停执行指定秒数
 * 用法：sleep NUMBER[SUFFIX]...
 * SUFFIX: s (秒), m (分), h (时), d (天)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char version[] = "0.1.0-sleep (meuos-utils)";

static double parse_duration(const char *s) {
    char *end;
    double val = strtod(s, &end);
    if (end == s) return -1;
    if (*end == '\0' || !strcmp(end, "s")) return val;
    if (!strcmp(end, "m")) return val * 60;
    if (!strcmp(end, "h")) return val * 3600;
    if (!strcmp(end, "d")) return val * 86400;
    return -1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("sleep %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: sleep NUMBER[SUFFIX]...\n"); return 0; }
    if (argc < 2) { fprintf(stderr, "sleep: missing operand\n"); return 2; }
    double total = 0;
    for (int i = 1; i < argc; i++) {
        double d = parse_duration(argv[i]);
        if (d < 0) { fprintf(stderr, "sleep: invalid time interval '%s'\n", argv[i]); return 2; }
        total += d;
    }
    if (total <= 0) return 0;
    unsigned int secs = (unsigned int)total;
    unsigned long nsecs = (unsigned long)((total - secs) * 1000000000UL);
    struct timespec ts = { secs, nsecs };
    nanosleep(&ts, NULL);
    return 0;
}
