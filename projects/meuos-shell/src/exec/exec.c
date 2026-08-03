/* msh/exec/exec.c — 全局状态 + 工具函数 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh/exec.h"

int msh_last_status = 0;
char **msh_argv = NULL;
static int exit_code = 0;

void msh_set_exit(int rc) { exit_code = rc; }

char *msh_which(const char *name) {
    if (!name) return NULL;
    if (strchr(name, '/')) return strdup(name);
    const char *path = getenv("PATH");
    if (!path) return NULL;
    char *dup = strdup(path);
    if (!dup) return NULL;
    char *save = NULL;
    char *dir = strtok_r(dup, ":", &save);
    char *full = NULL;
    while (dir) {
        size_t n = strlen(dir) + 1 + strlen(name) + 1;
        char *try = malloc(n);
        if (!try) break;
        snprintf(try, n, "%s/%s", dir, name);
        if (access(try, X_OK) == 0) {
            free(dup);
            free(full);
            return try;
        }
        free(try);
        dir = strtok_r(NULL, ":", &save);
    }
    free(dup);
    free(full);
    return NULL;
}
