/* cut — 字段/字符切割（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static int flag_chars = 0;
static int flag_fields = 0;
static char delimiter = '\t';
static char *range = NULL;  /* "1-3,5,7-" */

static void parse_range(int *arr, int *n_out, const char *spec, int max) {
    /* 简化：仅支持 "N" "N-M" "N-" "-M" */
    int n = 0;
    const char *p = spec;
    while (*p && n < 1000) {
        int a = 0, b = 0, has_a = 0, has_b = 0;
        while (*p == ',') p++;
        if (*p >= '0' && *p <= '9') {
            while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; has_a = 1; }
        }
        if (*p == '-') {
            p++;
            if (*p >= '0' && *p <= '9') {
                while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; has_b = 1; }
            }
        } else {
            b = a; has_b = 1;
        }
        if (has_a && has_b) {
            for (int i = a; i <= b; i++) arr[n++] = i;
        } else if (has_a) {
            arr[n++] = a;
        }
    }
    *n_out = n;
}

static int in_range(int *r, int n, int v) {
    for (int i = 0; i < n; i++) if (r[i] == v) return 1;
    return 0;
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [FILE...]\n\n", program_name);
    printf("Remove sections from each line.\n");
    printf("  -c, --characters LIST  select characters\n");
    printf("  -f, --fields LIST      select fields (default delim=Tab)\n");
    printf("  -d, --delimiter CHAR   field delimiter\n");
    printf("      --help             show this help\n");
    printf("      --version          show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    int opt;
    static const struct option longopts[] = {
        { "characters", required_argument, NULL, 'c' },
        { "fields",     required_argument, NULL, 'f' },
        { "delimiter",  required_argument, NULL, 'd' },
        { "help",       no_argument, NULL, 'h' },
        { "version",    no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "c:f:d:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': flag_chars = 1; range = optarg; break;
        case 'f': flag_fields = 1; range = optarg; break;
        case 'd': delimiter = optarg[0]; break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    if (!range) { fprintf(stderr, "%s: need -c or -f\n", program_name); return 1; }
    int r[1000]; int n;
    parse_range(r, &n, range, 1000);

    FILE *fp = stdin;
    if (optind < argc) fp = fopen(argv[optind], "r");
    char *line = NULL; size_t cap = 0; ssize_t L;
    while ((L = getline(&line, &cap, fp)) >= 0) {
        if (L > 0 && line[L-1] == '\n') line[--L] = '\0';
        if (flag_chars) {
            for (int i = 0; i < (int)L; i++) {
                if (in_range(r, n, i + 1)) fputc(line[i], stdout);
            }
            fputc('\n', stdout);
        } else {
            /* 按 delimiter 切 */
            int field = 1;
            int started = 0;
            int need_delim = 0;  /* 是否需要在下一个字段前输出分隔符 */
            for (int i = 0; i <= (int)L; i++) {
                int at_end = (i == (int)L);
                if (at_end || line[i] == delimiter) {
                    started = 0;
                    field++;
                } else {
                    if (!started && in_range(r, n, field)) {
                        if (need_delim) fputc(delimiter, stdout);
                        started = 1;
                        need_delim = 1;
                    }
                    if (started) fputc(line[i], stdout);
                }
            }
            fputc('\n', stdout);
        }
    }
    return 0;
}
