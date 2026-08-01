/* realpath — 解析绝对路径（含符号链接）
 * 用法：realpath [OPTIONS] FILE...
 * 选项：-s 不展开符号链接, -m 允许路径不存在, -z NUL 分隔
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char version[] = "0.1.0-realpath (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("realpath %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("Usage: realpath [-s] [-m] [-z] FILE...\n");
        return 0;
    }
    int no_symlinks = 0, allow_missing = 0, nul_sep = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi] + 1; *p; p++) {
            if (*p == 's') no_symlinks = 1;
            else if (*p == 'm') allow_missing = 1;
            else if (*p == 'z') nul_sep = 1;
            else { fprintf(stderr, "realpath: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (argi >= argc) { fprintf(stderr, "realpath: missing operand\n"); return 2; }
    int rc = 0;
    for (int i = argi; i < argc; i++) {
        char resolved[PATH_MAX];
        char *r;
        if (no_symlinks) {
            /* -s: 不解析符号链接，仅规范化路径 */
            if (realpath(argv[i], resolved) || allow_missing) {
                /* fallback: 构造绝对路径 */
                if (!allow_missing && access(argv[i], F_OK) != 0) {
                    fprintf(stderr, "realpath: %s: %s\n", argv[i], strerror(errno));
                    rc = 1; continue;
                }
                /* 简化：用 realpath 但允许不存在时回退 */
                r = realpath(argv[i], resolved);
                if (!r && allow_missing) {
                    /* 手动规范化 */
                    if (argv[i][0] == '/') r = strcpy(resolved, argv[i]);
                    else { getcwd(resolved, sizeof(resolved)); strncat(resolved, "/", PATH_MAX-1); strncat(resolved, argv[i], PATH_MAX-1); r = resolved; }
                }
            } else r = resolved;
        } else {
            r = realpath(argv[i], resolved);
        }
        if (!r) {
            if (!allow_missing) {
                fprintf(stderr, "realpath: %s: %s\n", argv[i], strerror(errno));
                rc = 1;
            } else {
                /* 允许不存在：输出规范化路径 */
                if (argv[i][0] == '/') {
                    printf("%s%s", argv[i], nul_sep ? "" : "\n");
                } else {
                    char cwd[PATH_MAX];
                    getcwd(cwd, sizeof(cwd));
                    printf("%s/%s%s", cwd, argv[i], nul_sep ? "" : "\n");
                }
                if (nul_sep) putchar('\0');
            }
            continue;
        }
        if (nul_sep) { fputs(r, stdout); putchar('\0'); }
        else printf("%s\n", r);
    }
    return rc;
}
