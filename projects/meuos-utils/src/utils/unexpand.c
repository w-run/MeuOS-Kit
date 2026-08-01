/* unexpand — 将空格转换为制表符
 * 用法：unexpand [-a] [-t N] [FILE]...
 * 选项：-a 转换所有空格(默认仅前导), -t N 制表符宽度(默认8)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = "0.1.0-unexpand (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("unexpand %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: unexpand [-a] [-t N] [FILE]...\n"); return 0; }
    int all = 0, tabsize = 8, argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-a")) { all = 1; argi++; }
        else if (!strcmp(argv[argi], "-t") && argi + 1 < argc) { tabsize = atoi(argv[argi+1]); argi += 2; }
        else break;
    }
    if (tabsize <= 0) tabsize = 8;
    int rc = 0;
    int nfiles = argc - argi;
    for (int fi = 0; fi < (nfiles > 0 ? nfiles : 1); fi++) {
        FILE *f;
        if (nfiles == 0) f = stdin;
        else { f = fopen(argv[argi + fi], "r"); if (!f) { fprintf(stderr, "unexpand: %s: %s\n", argv[argi+fi], strerror(errno)); rc = 1; continue; } }
        char *line = NULL; size_t cap = 0; ssize_t len;
        while ((len = getline(&line, &cap, f)) >= 0) {
            int col = 0;
            int leading = 1;
            int spaces = 0;
            for (ssize_t i = 0; i < len; i++) {
                if (line[i] == ' ' && (leading || all)) {
                    spaces++;
                    col++;
                    if (col % tabsize == 0) {
                        putchar('\t');
                        spaces = 0;
                    }
                } else if (line[i] == ' ') {
                    for (int s = 0; s < spaces; s++) putchar(' ');
                    spaces = 0;
                    putchar(' ');
                    col++;
                } else {
                    for (int s = 0; s < spaces; s++) putchar(' ');
                    spaces = 0;
                    if (line[i] != '\n') leading = 0;
                    putchar(line[i]);
                    if (line[i] == '\n') { leading = 1; col = 0; }
                    else col++;
                }
            }
            for (int s = 0; s < spaces; s++) putchar(' ');
        }
        free(line);
        if (f != stdin) fclose(f);
    }
    return rc;
}
