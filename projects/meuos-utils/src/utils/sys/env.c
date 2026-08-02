/* env — 在修改后的环境中运行命令
 * 用法：env [-i] [-u NAME] [-NAME] [NAME=VALUE...] [COMMAND [ARG]...]
 * 简化版：仅支持基本功能
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static const char version[] = "0.1.0-env (meuos-utils)";

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--version"))) {
        printf("env %s\n", version);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--help"))) {
        printf("Usage: env [-i] [NAME=VALUE]... [COMMAND [ARG]...]\n");
        return 0;
    }

    int argi = 1;
    int ignore_env = 0;

    /* 选项 */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-i")) {
            ignore_env = 1;
            argi++;
        } else if (!strncmp(argv[argi], "-u", 2)) {
            const char *name = argv[argi][2] ? argv[argi] + 2 : argv[argi + 1];
            if (name == argv[argi] + 2) {
                /* -uNAME */
            } else {
                argi++;
            }
            unsetenv(name);
            argi++;
        } else if (!strcmp(argv[argi], "-")) {
            ignore_env = 1;
            argi++;
            break;
        } else {
            break;
        }
    }

    if (ignore_env) {
        environ = NULL;
    }

    /* NAME=VALUE 对 */
    while (argi < argc && strchr(argv[argi], '=')) {
        char *eq = strchr(argv[argi], '=');
        *eq = '\0';
        setenv(argv[argi], eq + 1, 1);
        *eq = '=';
        argi++;
    }

    if (argi >= argc) {
        /* 无命令：打印环境 */
        for (char **e = environ; *e; e++) {
            printf("%s\n", *e);
        }
        return 0;
    }

    execvp(argv[argi], &argv[argi]);
    fprintf(stderr, "env: %s: %s\n", argv[argi], strerror(errno));
    return 127;
}
