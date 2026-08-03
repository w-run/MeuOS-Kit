/* nl — 行号输出
 * 用法：nl [OPTION]... [FILE]...
 * 选项：-b STYLE 行号样式(a/t/n), -n FORMAT(ln/rn/rz), -w WIDTH, -s SEP, -v START
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) {
        printf("Usage: nl [-b STYLE] [-n FORMAT] [-w WIDTH] [-s SEP] [-v START] [FILE]...\n");
        return 0;
    }
    int body_style = 't';  /* t=非空行编号, a=所有行, n=不编号 */
    const char *num_fmt = "rn";  /* rn=右对齐前导空格, rz=右对齐前导零, ln=左对齐 */
    int width = 6;
    const char *sep = "\t";
    long line_no = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-b") && argi + 1 < argc) { body_style = argv[argi+1][0]; argi += 2; }
        else if (!strcmp(argv[argi], "-n") && argi + 1 < argc) { num_fmt = argv[argi+1]; argi += 2; }
        else if (!strcmp(argv[argi], "-w") && argi + 1 < argc) { width = atoi(argv[argi+1]); argi += 2; }
        else if (!strcmp(argv[argi], "-s") && argi + 1 < argc) { sep = argv[argi+1]; argi += 2; }
        else if (!strcmp(argv[argi], "-v") && argi + 1 < argc) { line_no = atol(argv[argi+1]); argi += 2; }
        else break;
    }
    FILE *fp;
    if (argi >= argc) fp = stdin;
    else if (argc - argi == 1) fp = fopen(argv[argi], "r");
    else fp = NULL;  /* 多文件稍后处理 */

    int rc = 0;
    int nfiles = argc - argi;
    for (int fi = 0; fi < (nfiles > 0 ? nfiles : 1); fi++) {
        FILE *f;
        if (nfiles == 0) f = stdin;
        else { f = fopen(argv[argi + fi], "r"); if (!f) { fprintf(stderr, "nl: %s: %s\n", argv[argi+fi], strerror(errno)); rc = 1; continue; } }
        char *line = NULL; size_t cap = 0; ssize_t len;
        while ((len = getline(&line, &cap, f)) >= 0) {
            int do_num = 0;
            if (body_style == 'a') do_num = 1;
            else if (body_style == 't') do_num = (len > 1 || (len == 1 && line[0] != '\n'));
            else if (body_style == 'n') do_num = 0;

            if (do_num) {
                char nbuf[32];
                int sl = snprintf(nbuf, sizeof(nbuf), "%ld", line_no++);
                if (num_fmt[0] == 'r' && num_fmt[1] == 'z') {
                    /* 右对齐前导零 */
                    for (int i = sl; i < width; i++) putchar('0');
                    fputs(nbuf, stdout);
                } else if (num_fmt[0] == 'r') {
                    /* 右对齐前导空格 */
                    for (int i = sl; i < width; i++) putchar(' ');
                    fputs(nbuf, stdout);
                } else {
                    /* 左对齐 */
                    fputs(nbuf, stdout);
                    for (int i = sl; i < width; i++) putchar(' ');
                }
                fputs(sep, stdout);
            } else {
                for (int i = 0; i < width; i++) putchar(' ');
                fputs(sep, stdout);
            }
            fputs(line, stdout);
        }
        free(line);
        if (f != stdin) fclose(f);
    }
    return rc;
}
