/* tr — 字符替换/删除（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static int flag_delete = 0;
static int flag_squeeze = 0;

static void usage(void) {
    printf("Usage: %s [OPTIONS] SET1 [SET2]\n\n", program_name);
    printf("Translate or delete characters.\n");
    printf("  -d, --delete     delete chars in SET1\n");
    printf("  -s, --squeeze    squeeze repeats\n");
    printf("      --help       show this help\n");
    printf("      --version    show version\n");
}

/* 解析字符类如 [:alpha:] 等 + 范围 a-z */
static int match_class(int c, const char *set, int *idx_out) {
    int idx = 0;
    for (const char *p = set; *p; p++) {
        unsigned char cur = (unsigned char)*p;
        if (p[1] == '-' && p[2] != '\0' && (unsigned char)p[2] > cur) {
            unsigned char lo = cur, hi = (unsigned char)p[2];
            if (c >= lo && c <= hi) { if (idx_out) *idx_out = idx + (c - lo); return 1; }
            idx += hi - lo + 1;
            p += 2;
            continue;
        }
        if (cur == c) { if (idx_out) *idx_out = idx; return 1; }
        idx++;
    }
    return 0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    int opt;
    static const struct option longopts[] = {
        { "delete",  no_argument, NULL, 'd' },
        { "squeeze", no_argument, NULL, 's' },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "dsh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'd': flag_delete = 1; break;
        case 's': flag_squeeze = 1; break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "%s: missing SET\n", program_name);
        usage();
        return 1;
    }
    const char *set1 = argv[optind];
    const char *set2 = (optind + 1 < argc) ? argv[optind + 1] : NULL;
    int c;
    int prev = -1;
    while ((c = fgetc(stdin)) != EOF) {
        int idx = 0;
        int in_set1 = match_class(c, set1, &idx);
        if (flag_delete) {
            if (!in_set1) {
                if (!flag_squeeze || c != prev) fputc(c, stdout);
                prev = c;
            }
            continue;
        }
        if (in_set1 && set2) {
            /* 取 SET2 中 idx-th 字符；SET2 也支持范围 */
            int j = 0;
            for (const char *q = set2; *q; q++) {
                unsigned char cur = (unsigned char)*q;
                if (q[1] == '-' && q[2] != '\0' && (unsigned char)q[2] > cur) {
                    unsigned char lo = cur, hi = (unsigned char)q[2];
                    int range_sz = hi - lo + 1;
                    if (j + range_sz > idx) {
                        c = lo + (idx - j);
                        break;
                    }
                    j += range_sz;
                    q += 2;
                    continue;
                }
                if (j == idx) { c = cur; break; }
                j++;
            }
        }
        if (flag_squeeze && c == prev) continue;
        fputc(c, stdout);
        prev = c;
    }
    return 0;
}
