/* date — 显示/设置日期时间
 * 用法：date [+FORMAT]
 *       date -u [+FORMAT]
 *       date -d STRING [+FORMAT]
 * 选项：-u UTC, -d STRING 解析时间字符串, -R RFC 2822 格式, -I ISO 8601 格式
 */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "meuos/utils.h"

static void print_format(const struct tm *tm, const char *fmt) {
    char buf[4096];
    strftime(buf, sizeof(buf), fmt, tm);
    puts(buf);
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: date [-u] [+FORMAT] [-d STRING] [-R] [-I[FMT]]\n");
    int utc = 0;
    const char *fmt = "%a %b %e %H:%M:%S %Z %Y";  /* 默认 C locale 格式 */
    time_t t = time(NULL);
    struct tm tm;

    for (int i = argi; i < argc; i++) {
        if (!strcmp(argv[i], "-u")) utc = 1;
        else if (!strcmp(argv[i], "-R")) fmt = "%a, %d %b %Y %H:%M:%S %z";
        else if (!strncmp(argv[i], "-I", 2)) {
            const char *f = argv[i] + 2;
            if (!*f || !strcmp(f, "date")) fmt = "%Y-%m-%d";
            else if (!strcmp(f, "hours") || !strcmp(f, "seconds")) fmt = "%Y-%m-%dT%H:%M:%S%z";
            else if (!strcmp(f, "minutes")) fmt = "%Y-%m-%dT%H:%M%z";
            else fmt = "%Y-%m-%d";
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            /* 简化：解析 ISO 8601 格式 */
            struct tm tmp = {0};
            if (strptime(argv[++i], "%Y-%m-%d %H:%M:%S", &tmp) ||
                strptime(argv[i], "%Y-%m-%d", &tmp)) {
                t = mktime(&tmp);
            } else {
                /* 尝试 epoch 秒数 */
                char *end;
                long val = strtol(argv[i], &end, 10);
                if (*end == '\0') t = (time_t)val;
                else { fprintf(stderr, "date: invalid date '%s'\n", argv[i]); return 1; }
            }
        } else if (argv[i][0] == '+') {
            fmt = argv[i] + 1;
        }
    }

    if (utc) gmtime_r(&t, &tm);
    else localtime_r(&t, &tm);

    print_format(&tm, fmt);
    return 0;
}
