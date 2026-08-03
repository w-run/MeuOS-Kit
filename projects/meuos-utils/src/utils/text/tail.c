/* tail — 输出后 N 行 / 字节（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    long nlines = 10;
    long nbytes = -1;
    int follow = 0;
    int from_start = 0;  /* +N: 从第 N 行/字节开始 */
    int opt;
    static const struct option longopts[] = {
        { "lines",   required_argument, NULL, 'n' },
        { "bytes",   required_argument, NULL, 'c' },
        { "follow",  no_argument,       NULL, 'f' },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "n:c:fh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'n':
            if (optarg[0] == '+') {
                from_start = 1;
                nlines = atol(optarg + 1);
            } else {
                nlines = atol(optarg);
            }
            break;
        case 'c':
            if (optarg[0] == '+') {
                from_start = 1;
                nbytes = atol(optarg + 1);
            } else {
                nbytes = atol(optarg);
            }
            break;
        case 'f': follow = 1; break;
        case 'h': printf("Usage: tail [-n N | -c N] [FILE]\n"); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    FILE *fp = stdin;
    if (optind < argc) {
        fp = fopen(argv[optind], "r");
        if (!fp) { perror(argv[optind]); return 1; }
    }
    /* 使用循环缓冲区（行模式） */
    if (nbytes < 0) {
        if (from_start) {
            /* +N 模式：从第 N 行开始输出到结尾 */
            char *line = NULL; size_t cap = 0; ssize_t n;
            long lineno = 0;
            while ((n = getline(&line, &cap, fp)) >= 0) {
                lineno++;
                if (lineno >= nlines) {
                    fputs(line, stdout);
                }
            }
            free(line);
        } else {
            /* 默认模式：输出最后 N 行 */
            if (nlines <= 0) nlines = 10;
            char **ring = xmalloc(sizeof(char *) * nlines);
            for (long i = 0; i < nlines; i++) ring[i] = NULL;
            char *line = NULL; size_t cap = 0; ssize_t n;
            long idx = 0;
            long total = 0;
            while ((n = getline(&line, &cap, fp)) >= 0) {
                if (ring[idx]) free(ring[idx]);
                ring[idx] = xstrdup(line);
                idx = (idx + 1) % nlines;
                total++;
            }
            long start = total < nlines ? 0 : idx;
            for (long i = 0; i < (total < nlines ? total : nlines); i++) {
                fputs(ring[(start + i) % nlines], stdout);
            }
            for (long i = 0; i < nlines; i++) free(ring[i]);
            free(ring);
            free(line);
        }
    } else {
        /* 字节模式：用 ring buffer 实现尾部 N 字节 */
        char *buf = xmalloc(nbytes);
        long pos = 0;
        int c;
        while ((c = fgetc(fp)) != EOF) {
            buf[pos % nbytes] = (char)c;
            pos++;
        }
        long start = pos > nbytes ? pos - nbytes : 0;
        for (long i = start; i < pos; i++) fputc(buf[i % nbytes], stdout);
        free(buf);
    }
    /* -f 监控：循环 poll */
    if (follow) {
        char buf[4096];
        while (1) {
            size_t n = fread(buf, 1, sizeof(buf), fp);
            if (n == 0) { usleep(200000); continue; }
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }
    }
    if (fp != stdin) fclose(fp);
    return 0;
}
