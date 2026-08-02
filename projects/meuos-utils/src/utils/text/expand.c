/* expand — 将制表符转换为空格
 * 用法：expand [-t LIST] [FILE]...
 * 选项：-t LIST 制表符位置(逗号分隔), 默认 8
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = "0.1.0-expand (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("expand %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: expand [-t LIST] [FILE]...\n"); return 0; }
    int tabsize = 8;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-t") && argi + 1 < argc) { tabsize = atoi(argv[argi+1]); argi += 2; }
        else if (argv[argi][1] == 't' && argv[argi][2] >= '0' && argv[argi][2] <= '9') { tabsize = atoi(argv[argi]+2); argi++; }
        else break;
    }
    if (tabsize <= 0) tabsize = 8;
    int rc = 0;
    int nfiles = argc - argi;
    for (int fi = 0; fi < (nfiles > 0 ? nfiles : 1); fi++) {
        FILE *f;
        if (nfiles == 0) f = stdin;
        else { f = fopen(argv[argi + fi], "r"); if (!f) { fprintf(stderr, "expand: %s: %s\n", argv[argi+fi], strerror(errno)); rc = 1; continue; } }
        int col = 0;
        int c;
        while ((c = fgetc(f)) != EOF) {
            if (c == '\t') {
                int spaces = tabsize - (col % tabsize);
                for (int i = 0; i < spaces; i++) putchar(' ');
                col += spaces;
            } else if (c == '\n') {
                putchar(c);
                col = 0;
            } else {
                putchar(c);
                col++;
            }
        }
        if (f != stdin) fclose(f);
    }
    return rc;
}
