/* sort — 行排序（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static int flag_reverse = 0;
static int flag_numeric = 0;
static int flag_unique = 0;
static int flag_ignore_blanks = 0;

static int strcmp_sort(const char *a, const char *b, int ignore_blanks) {
    while (ignore_blanks && *a == ' ') a++;
    while (ignore_blanks && *b == ' ') b++;
    return strcmp(a, b);
}

static int num_cmp(const char *a, const char *b) {
    double da = atof(a), db = atof(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static int sort_cmp(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    int r;
    if (flag_numeric) r = num_cmp(sa, sb);
    else r = strcmp_sort(sa, sb, flag_ignore_blanks);
    if (flag_reverse) r = -r;
    return r;
}

static char **read_lines(FILE *fp, size_t *n_out) {
    size_t cap = 64, n = 0;
    char **arr = xmalloc(cap * sizeof(char *));
    char *line = NULL; size_t lcap = 0; ssize_t L;
    while ((L = getline(&line, &lcap, fp)) >= 0) {
        if (n >= cap) { cap *= 2; arr = xrealloc(arr, cap * sizeof(*arr)); }
        char *s = xmalloc(L + 1);
        memcpy(s, line, L);
        s[L] = '\0';
        arr[n++] = s;
    }
    free(line);
    *n_out = n;
    return arr;
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [FILE...]\n\n", program_name);
    printf("Sort lines of text.\n");
    printf("  -r, --reverse         reverse sort\n");
    printf("  -n, --numeric         numeric sort\n");
    printf("  -u, --unique          output unique\n");
    printf("  -b, --ignore-blanks   ignore leading blanks\n");
    printf("      --help            show this help\n");
    printf("      --version         show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    int opt;
    static const struct option longopts[] = {
        { "reverse",       no_argument, NULL, 'r' },
        { "numeric",       no_argument, NULL, 'n' },
        { "unique",        no_argument, NULL, 'u' },
        { "ignore-blanks", no_argument, NULL, 'b' },
        { "help",          no_argument, NULL, 'h' },
        { "version",       no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "rnubh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'r': flag_reverse = 1; break;
        case 'n': flag_numeric = 1; break;
        case 'u': flag_unique = 1; break;
        case 'b': flag_ignore_blanks = 1; break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    size_t n = 0;
    char **arr = NULL;
    if (optind >= argc) {
        arr = read_lines(stdin, &n);
    } else {
        for (int i = optind; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) { perror(argv[i]); continue; }
            size_t m = 0;
            char **sub = read_lines(fp, &m);
            fclose(fp);
            arr = xrealloc(arr, sizeof(char *) * (n + m + 1));
            for (size_t j = 0; j < m; j++) arr[n + j] = sub[j];
            n += m;
            free(sub);
        }
    }
    if (!arr) return 0;
    qsort(arr, n, sizeof(char *), sort_cmp);
    for (size_t i = 0; i < n; i++) {
        if (flag_unique && i > 0 && strcmp(arr[i], arr[i-1]) == 0) continue;
        fputs(arr[i], stdout);
    }
    return 0;
}
