/* dirname — 提取路径中的目录部分
 * 用法：dirname NAME...
 * 选项：-z NUL 分隔输出
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


static char *dir_name(const char *path) {
    size_t len = strlen(path);
    if (len == 0) return strdup(".");
    /* 去掉尾部 / */
    while (len > 1 && path[len - 1] == '/') len--;
    /* 找最后一个 / */
    const char *slash = NULL;
    for (size_t i = 0; i < len; i++)
        if (path[i] == '/') slash = &path[i];
    if (!slash) return strdup(".");
    size_t dlen = (size_t)(slash - path);
    if (dlen == 0) return strdup("/");
    char *r = malloc(dlen + 1);
    memcpy(r, path, dlen);
    r[dlen] = '\0';
    return r;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: dirname [-z] NAME...\n"); return 0; }
    int nul_sep = 0;
    if (argi < argc && !strcmp(argv[argi], "-z")) { nul_sep = 1; argi++; }
    if (argi >= argc) { fprintf(stderr, "dirname: missing operand\n"); return 2; }
    for (int i = argi; i < argc; i++) {
        char *d = dir_name(argv[i]);
        if (nul_sep) { fputs(d, stdout); putchar('\0'); }
        else printf("%s\n", d);
        free(d);
    }
    return 0;
}
