/* paste — 合并多个文件的行
 * 用法：paste [OPTION]... [FILE]...
 * 选项：-d LIST 使用 LIST 中的字符作为分隔符(默认\t), -s 串行而非并行
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: paste [-d LIST] [-s] [FILE]...\n"); return 0; }
    const char *delims = "\t";
    int serial = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-d") && argi + 1 < argc) { delims = argv[++argi]; argi++; }
        else if (!strcmp(argv[argi], "-s")) { serial = 1; argi++; }
        else break;
    }
    if (argi >= argc) { argv[argi] = "-"; argc++; }  /* 默认 stdin */
    int nfiles = argc - argi;
    FILE **fps = calloc(nfiles, sizeof(FILE *));
    for (int i = 0; i < nfiles; i++) {
        if (!strcmp(argv[argi+i], "-")) fps[i] = stdin;
        else { fps[i] = fopen(argv[argi+i], "r"); if (!fps[i]) { fprintf(stderr, "paste: %s: %s\n", argv[argi+i], strerror(errno)); exit(1); } }
    }
    if (serial) {
        /* 串行：每次处理一个文件的所有行，用分隔符连接 */
        for (int i = 0; i < nfiles; i++) {
            char *line = NULL; size_t cap = 0; ssize_t len;
            int first = 1;
            while ((len = getline(&line, &cap, fps[i])) >= 0) {
                if (!first) putchar(delims[(i) % strlen(delims) ? (i) % strlen(delims) : 0]);
                if (!first) putchar(delims[0]);  /* 简化：用第一个分隔符 */
                fputs(line, stdout);
                first = 0;
            }
            free(line);
        }
    } else {
        /* 并行：同时读取所有文件的同一行 */
        char **lines = calloc(nfiles, sizeof(char *));
        size_t *caps = calloc(nfiles, sizeof(size_t));
        while (1) {
            int any = 0;
            for (int i = 0; i < nfiles; i++) {
                ssize_t len = getline(&lines[i], &caps[i], fps[i]);
                if (len >= 0) {
                    any = 1;
                    if (len > 0 && lines[i][len-1] == '\n') lines[i][--len] = '\0';
                } else { lines[i] = ""; }
            }
            if (!any) break;
            for (int i = 0; i < nfiles; i++) {
                if (i > 0) putchar(delims[(i-1) % strlen(delims)]);
                fputs(lines[i], stdout);
            }
            putchar('\n');
        }
        free(lines); free(caps);
    }
    for (int i = 0; i < nfiles; i++) if (fps[i] && fps[i] != stdin) fclose(fps[i]);
    free(fps);
    return 0;
}
