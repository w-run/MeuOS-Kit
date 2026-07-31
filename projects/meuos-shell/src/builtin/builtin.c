/* msh/builtin/builtin.c — 内建命令实现
 *
 * 现阶段只实现 cd/export/unset/set。其他内建（exit/echo/true/false/:）
 * 由 parse.c 直接实现以减少文件耦合。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh/msh.h"

int msh_builtin_cd(int argc, char **argv) {
    const char *target = NULL;
    if (argc == 1) {
        target = getenv("HOME");
        if (!target) target = "/";
    } else if (argc == 2) {
        target = argv[1];
        if (target[0] == '-' && target[1] == '\0') {
            target = getenv("OLDPWD");
        }
    } else {
        fprintf(stderr, "msh: cd: too many arguments\n");
        return 2;
    }
    if (chdir(target) < 0) {
        fprintf(stderr, "msh: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    /* 更新 PWD */
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) setenv("PWD", buf, 1);
    return 0;
}

int msh_builtin_export(int argc, char **argv) {
    if (argc == 1) {
        extern char **environ;
        for (char **e = environ; *e; e++) puts(*e);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        } else {
            /* 标记已有变量为 export（POSIX 标准为标记，本骨架已统一 setenv） */
            if (getenv(argv[i])) {
                /* already in environ */
            } else {
                fprintf(stderr, "msh: export: %s: not found\n", argv[i]);
                return 1;
            }
        }
    }
    return 0;
}

int msh_builtin_unset(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        unsetenv(argv[i]);
    }
    return 0;
}

int msh_builtin_set(int argc, char **argv) {
    (void)argv;
    if (argc == 1) {
        extern char **environ;
        for (char **e = environ; *e; e++) puts(*e);
        return 0;
    }
    /* 简化：`set VAR=val` 形式 */
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        } else {
            fprintf(stderr, "msh: set: %s: not a name=value\n", argv[i]);
        }
    }
    return 0;
}
