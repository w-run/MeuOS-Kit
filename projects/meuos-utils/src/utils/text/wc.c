/* wc — 字节/行/词统计（MeuOS Next 版）
 *
 * 默认：行 + 词 + 字节
 * 选项：
 *   -l, -w, -c, -m (chars), -L (longest line)
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static int flag_lines = 0;
static int flag_words = 0;
static int flag_bytes = 0;
static int flag_chars = 0;
static int flag_max = 0;

static void count(FILE *fp, const char *name, long *lines, long *words,
                  long *bytes, long *maxlen, long *chars) {
    int c;
    int in_word = 0;
    long L = 0, W = 0, B = 0, M = 0, Ch = 0;
    int curlen = 0;
    while ((c = fgetc(fp)) != EOF) {
        B++;
        if (c == '\n') { L++; if (curlen > M) M = curlen; curlen = 0; }
        else curlen++;
        Ch++;
        if (isspace(c)) {
            if (in_word) { in_word = 0; }
        } else {
            if (!in_word) { in_word = 1; W++; }
        }
    }
    if (curlen > M) M = curlen;  /* last line without \n */
    *lines = L; *words = W; *bytes = B; *maxlen = M; *chars = Ch;
    (void)name;
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [FILE...]\n", program_name);
    printf("\n");
    printf("Print newline, word, and byte counts.\n");
    printf("  -l, --lines        newline counts\n");
    printf("  -w, --words        word counts\n");
    printf("  -c, --bytes        byte counts\n");
    printf("  -m, --chars        character counts\n");
    printf("  -L, --max-line-length\n");
    printf("      --help         show this help\n");
    printf("      --version      show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    int opt;
    static const struct option longopts[] = {
        { "lines",     no_argument, NULL, 'l' },
        { "words",     no_argument, NULL, 'w' },
        { "bytes",     no_argument, NULL, 'c' },
        { "chars",     no_argument, NULL, 'm' },
        { "max-line-length", no_argument, NULL, 'L' },
        { "help",      no_argument, NULL, 'h' },
        { "version",   no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "lwcmLh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'l': flag_lines = 1; break;
        case 'w': flag_words = 1; break;
        case 'c': flag_bytes = 1; break;
        case 'm': flag_chars = 1; break;
        case 'L': flag_max = 1; break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    int all = !(flag_lines || flag_words || flag_bytes || flag_chars || flag_max);
    if (all) { flag_lines = flag_words = flag_bytes = 1; }

    int nfiles = 0;
    long tl = 0, tw = 0, tb = 0, tc = 0, tm = 0;

    if (optind >= argc) {
        long l, w, b, m, c;
        count(stdin, "", &l, &w, &b, &m, &c);
        tl += l; tw += w; tb += b; tm += m; tc += c; nfiles++;
    } else {
        for (int i = optind; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) { perror(argv[i]); continue; }
            long l, w, b, m, c;
            count(fp, argv[i], &l, &w, &b, &m, &c);
            tl += l; tw += w; tb += b; tm += m; tc += c; nfiles++;
            int show_name = (argc - optind > 1);
            if (show_name) {
                int col = 0;
                if (flag_lines) { printf("%7ld ", l); col += 8; }
                if (flag_words) { printf("%7ld ", w); col += 8; }
                if (flag_chars) { printf("%7ld ", c); col += 8; }
                if (flag_bytes) { printf("%7ld ", b); col += 8; }
                if (flag_max)   { printf("%7ld ", m); col += 8; }
                printf(" %s\n", argv[i]);
            }
            fclose(fp);
        }
    }
    if (nfiles > 1) {
        int col = 0;
        if (flag_lines) { printf("%7ld ", tl); col += 8; }
        if (flag_words) { printf("%7ld ", tw); col += 8; }
        if (flag_chars) { printf("%7ld ", tc); col += 8; }
        if (flag_bytes) { printf("%7ld ", tb); col += 8; }
        if (flag_max)   { printf("%7ld ", tm); col += 8; }
        printf(" total\n");
    } else if (nfiles == 1 && optind >= argc) {
        /* stdin */
        int col = 0;
        if (flag_lines) { printf("%ld", tl); col = 1; }
        if (flag_words) { printf(col ? " %ld" : "%ld", tw); col = 1; }
        if (flag_chars) { printf(col ? " %ld" : "%ld", tc); col = 1; }
        if (flag_bytes) { printf(col ? " %ld" : "%ld", tb); col = 1; }
        if (flag_max)   { printf(col ? " %ld" : "%ld", tm); col = 1; }
        (void)col;
        fputc('\n', stdout);
    }
    return 0;
}
