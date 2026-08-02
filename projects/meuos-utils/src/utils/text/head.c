/* head — 输出前 N 行（MeuOS Next 版） */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    long nlines = 10;
    long nbytes = -1;
    int opt;
    static const struct option longopts[] = {
        { "lines",   required_argument, NULL, 'n' },
        { "bytes",   required_argument, NULL, 'c' },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };
    while ((opt = getopt_long(argc, argv, "n:c:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'n': nlines = atol(optarg); break;
        case 'c': nbytes = atol(optarg); break;
        case 'h': printf("Usage: head [-n N | -c N] [FILE]\n"); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }
    FILE *fp = stdin;
    const char *name = NULL;
    if (optind < argc) {
        fp = fopen(argv[optind], "r");
        if (!fp) { perror(argv[optind]); return 1; }
        name = argv[optind];
    }
    if (nbytes >= 0) {
        long copied = 0;
        int c;
        while (copied < nbytes && (c = fgetc(fp)) != EOF) {
            fputc(c, stdout);
            copied++;
        }
    } else {
        long L = 0;
        int c;
        int prev = '\n';
        while ((c = fgetc(fp)) != EOF) {
            if (L >= nlines && prev == '\n') break;
            if (c == '\n') L++;
            fputc(c, stdout);
            prev = c;
        }
    }
    if (fp != stdin) fclose(fp);
    (void)name;
    return 0;
}
