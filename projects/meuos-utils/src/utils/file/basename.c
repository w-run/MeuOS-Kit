/* basename — 提取路径中的文件名部分
 * 用法：basename NAME [SUFFIX]
 *       basename -a NAME...        (多参数)
 *       basename -s SUFFIX NAME...  (-a + -s 组合)
 * 选项：-a 多参数模式, -s SUFFIX 去后缀, -z NUL 分隔
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


static char *base_name(const char *path) {
    const char *p = path;
    /* 去掉尾部 / */
    const char *end = p + strlen(p);
    while (end > p + 1 && end[-1] == '/') end--;
    /* 找最后一个 / */
    const char *slash = end;
    while (slash > p && slash[-1] != '/') slash--;
    size_t len = (size_t)(end - slash);
    char *r = malloc(len + 1);
    memcpy(r, slash, len);
    r[len] = '\0';
    return r;
}

static char *strip_suffix(const char *name, const char *suffix) {
    size_t nl = strlen(name), sl = strlen(suffix);
    if (sl > 0 && nl > sl && strcmp(name + nl - sl, suffix) == 0) {
        char *r = malloc(nl - sl + 1);
        memcpy(r, name, nl - sl);
        r[nl - sl] = '\0';
        return r;
    }
    return strdup(name);
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) {
        printf("Usage: basename NAME [SUFFIX]\n"
               "  or:  basename -a [-s SUFFIX] NAME...\n");
        return 0;
    }
    int multi = 0, nul_sep = 0;
    const char *suffix = NULL;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-a")) { multi = 1; argi++; }
        else if (!strcmp(argv[argi], "-s")) { suffix = argv[++argi]; multi = 1; argi++; }
        else if (!strcmp(argv[argi], "-z")) { nul_sep = 1; argi++; }
        else break;
    }
    if (argi >= argc) { fprintf(stderr, "basename: missing operand\n"); return 2; }
    if (!multi) {
        const char *name = argv[argi];
        if (argc - argi >= 2) suffix = argv[argi + 1];
        char *b = base_name(name);
        if (suffix) { char *r = strip_suffix(b, suffix); free(b); b = r; }
        printf("%s\n", b);
        free(b);
    } else {
        for (int i = argi; i < argc; i++) {
            char *b = base_name(argv[i]);
            if (suffix) { char *r = strip_suffix(b, suffix); free(b); b = r; }
            if (nul_sep) { fputs(b, stdout); putchar('\0'); }
            else printf("%s\n", b);
            free(b);
        }
    }
    return 0;
}
