/* readlink — 读取符号链接目标
 * 用法：readlink [OPTIONS] FILE...
 * 选项：-f 递归解析, -e 存在才解析, -m 允许不存在, -n 不输出换行, -z NUL 分隔
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char version[] = "0.1.0-readlink (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("readlink %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("Usage: readlink [-f|-e|-m] [-n] [-z] FILE...\n");
        return 0;
    }
    int canonicalize = 0, exists = 0, allow_missing = 0, no_nl = 0, nul_sep = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi] + 1; *p; p++) {
            switch (*p) {
            case 'f': canonicalize = 1; allow_missing = 1; break;
            case 'e': canonicalize = 1; exists = 1; break;
            case 'm': canonicalize = 1; allow_missing = 1; break;
            case 'n': no_nl = 1; break;
            case 'z': nul_sep = 1; break;
            default: fprintf(stderr, "readlink: unknown option -%c\n", *p); return 2;
            }
        }
        argi++;
    }
    if (argi >= argc) { fprintf(stderr, "readlink: missing operand\n"); return 2; }
    int rc = 0;
    for (int i = argi; i < argc; i++) {
        if (canonicalize) {
            char resolved[PATH_MAX];
            char *r = realpath(argv[i], resolved);
            if (!r) {
                if (!allow_missing) {
                    fprintf(stderr, "readlink: %s: %s\n", argv[i], strerror(errno));
                    rc = 1; continue;
                }
                /* 允许不存在：输出路径 */
                if (argv[i][0] == '/') r = strcpy(resolved, argv[i]);
                else { getcwd(resolved, sizeof(resolved)); strcat(resolved, "/"); strcat(resolved, argv[i]); r = resolved; }
            }
            if (nul_sep) { fputs(r, stdout); putchar('\0'); }
            else { fputs(r, stdout); if (!no_nl) putchar('\n'); }
        } else {
            /* 读取符号链接目标 */
            char buf[PATH_MAX];
            ssize_t n = readlink(argv[i], buf, sizeof(buf) - 1);
            if (n < 0) {
                fprintf(stderr, "readlink: %s: %s\n", argv[i], strerror(errno));
                rc = 1; continue;
            }
            buf[n] = '\0';
            if (nul_sep) { fputs(buf, stdout); putchar('\0'); }
            else { fputs(buf, stdout); if (!no_nl) putchar('\n'); }
        }
    }
    return rc;
}
