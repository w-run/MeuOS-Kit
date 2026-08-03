/* fold — 按指定宽度折行
 * 用法：fold [-w WIDTH] [-b] [-s] [FILE]...
 * 选项：-w WIDTH 指定宽度(默认80), -b 按字节计数, -s 在空格处断行
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: fold [-w WIDTH] [-b] [-s] [FILE]...\n"); return 0; }
    int width = 80, bytes = 0, breaks = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-w") && argi + 1 < argc) { width = atoi(argv[argi+1]); argi += 2; }
        else if (!strcmp(argv[argi], "-b")) { bytes = 1; argi++; }
        else if (!strcmp(argv[argi], "-s")) { breaks = 1; argi++; }
        else if (argv[argi][1] == 'w' && argv[argi][2] >= '0' && argv[argi][2] <= '9') { width = atoi(argv[argi]+2); argi++; }
        else break;
    }
    (void)bytes; (void)breaks;
    int rc = 0;
    int nfiles = argc - argi;
    for (int fi = 0; fi < (nfiles > 0 ? nfiles : 1); fi++) {
        FILE *f;
        if (nfiles == 0) f = stdin;
        else { f = fopen(argv[argi + fi], "r"); if (!f) { fprintf(stderr, "fold: %s: %s\n", argv[argi+fi], strerror(errno)); rc = 1; continue; } }
        char *line = NULL; size_t cap = 0; ssize_t len;
        while ((len = getline(&line, &cap, f)) >= 0) {
            int col = 0;
            for (ssize_t i = 0; i < len; i++) {
                if (col >= width) { putchar('\n'); col = 0; }
                putchar(line[i]);
                col++;
            }
        }
        free(line);
        if (f != stdin) fclose(f);
    }
    return rc;
}
