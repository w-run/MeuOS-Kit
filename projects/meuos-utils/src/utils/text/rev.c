/* rev — 反转每行的字符
 * 用法：rev [FILE]...
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "meuos/utils.h"


int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: rev [FILE]...\n"); return 0; }
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') argi++;

    int rc = 0;
    int nfiles = argc - argi;
    for (int fi = 0; fi < (nfiles > 0 ? nfiles : 1); fi++) {
        FILE *f;
        if (nfiles == 0) f = stdin;
        else { f = fopen(argv[argi + fi], "r"); if (!f) { fprintf(stderr, "rev: %s: %s\n", argv[argi+fi], strerror(errno)); rc = 1; continue; } }
        char *line = NULL; size_t cap = 0; ssize_t len;
        while ((len = getline(&line, &cap, f)) >= 0) {
            /* 去掉换行 */
            int has_nl = 0;
            if (len > 0 && line[len-1] == '\n') { line[--len] = '\0'; has_nl = 1; }
            /* 反转 */
            for (int i = 0, j = (int)len - 1; i < j; i++, j--) {
                char c = line[i]; line[i] = line[j]; line[j] = c;
            }
            fputs(line, stdout);
            if (has_nl) putchar('\n');
        }
        free(line);
        if (f != stdin) fclose(f);
    }
    return rc;
}
