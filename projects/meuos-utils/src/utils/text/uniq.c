/* uniq — 去重相邻行（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static int flag_count = 0;
static int flag_dups  = 0;  /* -d: only print duplicate lines */
static int flag_uniq  = 0;  /* -u: only print unique lines */

static void usage(void) {
    printf("Usage: %s [OPTIONS] [INPUT [OUTPUT]]\n\n", program_name);
    printf("Filter adjacent matching lines.\n");
    printf("  -c, --count    prefix with occurrence count\n");
    printf("  -d, --repeated only print duplicate lines\n");
    printf("  -u, --unique   only print unique lines\n");
    printf("      --help     show this help\n");
    printf("      --version  show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    int opt;
    static const struct option longopts[] = {
        { "count",    no_argument, NULL, 'c' },
        { "repeated", no_argument, NULL, 'd' },
        { "unique",   no_argument, NULL, 'u' },
        { "help",     no_argument, NULL, 'h' },
        { "version",  no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "cduh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': flag_count = 1; break;
        case 'd': flag_dups  = 1; break;
        case 'u': flag_uniq  = 1; break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    FILE *in = stdin, *out = stdout;
    if (optind < argc) {
        in = fopen(argv[optind], "r");
        if (!in) { perror(argv[optind]); return 1; }
    }
    if (optind + 1 < argc) {
        out = fopen(argv[optind + 1], "w");
        if (!out) { perror(argv[optind + 1]); return 1; }
    }
    char *prev = NULL;
    char *line = NULL; size_t cap = 0; ssize_t n;
    long count = 0;
    while ((n = getline(&line, &cap, in)) >= 0) {
        if (prev == NULL || strcmp(prev, line) != 0) {
            /* 输出上一个组 */
            if (prev) {
                int is_dup = (count > 1);
                int should_print = 1;
                if (flag_dups && !is_dup) should_print = 0;
                if (flag_uniq && is_dup) should_print = 0;
                if (should_print) {
                    if (flag_count) fprintf(out, "%7ld %s", count, prev);
                    else fputs(prev, out);
                }
                free(prev);
            }
            prev = xstrdup(line);
            count = 1;
        } else {
            count++;
        }
    }
    /* 输出最后一组 */
    if (prev) {
        int is_dup = (count > 1);
        int should_print = 1;
        if (flag_dups && !is_dup) should_print = 0;
        if (flag_uniq && is_dup) should_print = 0;
        if (should_print) {
            if (flag_count) fprintf(out, "%7ld %s", count, prev);
            else fputs(prev, out);
        }
        free(prev);
    }
    if (in != stdin) fclose(in);
    if (out != stdout) fclose(out);
    return 0;
}
