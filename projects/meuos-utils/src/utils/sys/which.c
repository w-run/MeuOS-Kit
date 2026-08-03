/* which — 在 PATH 中查找命令的位置
 * 用法：which [-a] [-s] COMMAND...
 * 选项：-a 列出所有匹配, -s 静默模式（仅返回退出码）
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/utils.h"

static int find_in_path(const char *cmd, int all, int silent) {
    if (strchr(cmd, '/')) {
        /* 含路径分隔符：直接检查 */
        if (access(cmd, X_OK) == 0) {
            if (!silent) printf("%s\n", cmd);
            return 0;
        }
        return 1;
    }
    const char *path = getenv("PATH");
    if (!path || !*path) return 1;
    int found = 0;
    char *copy = strdup(path);
    char *dir = strtok(copy, ":");
    while (dir) {
        size_t dlen = strlen(dir);
        char *full = malloc(dlen + 1 + strlen(cmd) + 1);
        sprintf(full, "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) {
            struct stat st;
            if (stat(full, &st) == 0 && S_ISREG(st.st_mode)) {
                if (!silent) printf("%s\n", full);
                found = 1;
                if (!all) { free(full); break; }
            }
        }
        free(full);
        dir = strtok(NULL, ":");
    }
    free(copy);
    return found ? 0 : 1;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) utils_usage("Usage: which [-a] [-s] COMMAND...\n");
    int all = 0, silent = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'a') all = 1;
            else if (*p == 's') silent = 1;
            else { fprintf(stderr, "which: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (argi >= argc) { fprintf(stderr, "which: missing operand\n"); return 2; }
    int rc = 0;
    for (int i = argi; i < argc; i++)
        if (find_in_path(argv[i], all, silent) != 0) rc = 1;
    return rc;
}
