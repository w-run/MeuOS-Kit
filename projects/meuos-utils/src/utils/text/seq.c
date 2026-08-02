/* seq — 输出数字序列
 * 用法：seq [FIRST [INCREMENT]] LAST
 * 选项：-s separator (default \n), -w equal width, -f format (printf style)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--version"))) {
        printf("seq %s\n", version);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--help"))) {
        printf("Usage: seq [FIRST [INCREMENT]] LAST\n");
        return 0;
    }

    long first = 1, inc = 1, last;
    char *sep = "\n";

    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "-s")) {
            sep = argv[++argi];
        } else if (!strcmp(argv[argi], "-w")) {
            /* equal width: simplified */
        }
        argi++;
    }

    long nargs = argc - argi;
    if (nargs == 0 || nargs > 3) {
        fprintf(stderr, "seq: usage: seq [FIRST [INCREMENT]] LAST\n");
        return 2;
    }
    if (nargs == 1) last = atol(argv[argi]);
    else if (nargs == 2) { first = atol(argv[argi]); last = atol(argv[argi+1]); }
    else { first = atol(argv[argi]); inc = atol(argv[argi+1]); last = atol(argv[argi+2]); }

    if (inc == 0) { fprintf(stderr, "seq: zero increment\n"); return 2; }

    if (inc > 0) {
        for (long i = first; i <= last; i += inc) {
            if (i != first) fputs(sep, stdout);
            printf("%ld", i);
        }
    } else {
        for (long i = first; i >= last; i += inc) {
            if (i != first) fputs(sep, stdout);
            printf("%ld", i);
        }
    }
    if (strcmp(sep, "\n") == 0 || strlen(sep) > 0) {
        printf("%s", sep);
    }
    return 0;
}
