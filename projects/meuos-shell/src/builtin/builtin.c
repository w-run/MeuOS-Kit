/* msh/builtin/builtin.c — 内建命令实现
 *
 * cd/export/unset/set + getopts/shift/alias/unalias/local/umask/hash
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-e") == 0) {
                extern int msh_errexit;
                msh_errexit = 1;
            } else if (strcmp(argv[i], "+e") == 0) {
                extern int msh_errexit;
                msh_errexit = 0;
            } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                if (strcmp(argv[i+1], "pipefail") == 0) {
                    extern int msh_pipefail;
                    msh_pipefail = 1;
                    i++;
                } else if (strcmp(argv[i+1], "errexit") == 0) {
                    extern int msh_errexit;
                    msh_errexit = 1;
                    i++;
                } else {
                    i++;
                }
            } else if (strcmp(argv[i], "+o") == 0 && i + 1 < argc) {
                if (strcmp(argv[i+1], "pipefail") == 0) {
                    extern int msh_pipefail;
                    msh_pipefail = 0;
                    i++;
                } else if (strcmp(argv[i+1], "errexit") == 0) {
                    extern int msh_errexit;
                    msh_errexit = 0;
                    i++;
                } else {
                    i++;
                }
            } else {
                char *eq = strchr(argv[i], '=');
                if (eq) {
                    *eq = '\0';
                    setenv(argv[i], eq + 1, 1);
                    *eq = '=';
                }
            }
        }
        return 0;
    }
    (void)argv;
    if (argc == 1) {
        extern char **environ;
        for (char **e = environ; *e; e++) puts(*e);
        return 0;
    }
    return 0;
}

/* === getopts 内建 === */
/* 用法：getopts OPTSTRING NAME [ARG...]
 * 解析位置参数或 ARG 中的选项。
 * 设置 NAME 为选项字符，OPTARG 为参数值，OPTIND 为下一选项索引。
 * 返回 0=找到选项，1=无更多选项，2=错误。
 */
int msh_builtin_getopts(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "msh: getopts: usage: getopts optstring name [arg ...]\n");
        return 2;
    }
    const char *optstring = argv[1];
    const char *varname = argv[2];

    /* 获取 OPTIND */
    const char *optind_str = getenv("OPTIND");
    int optind_val = optind_str ? atoi(optind_str) : 1;
    if (optind_val < 1) optind_val = 1;

    /* 选项参数来源 */
    int nargs = argc - 3;
    char **args = &argv[3];

    /* 如果没有额外 args，从 $1..$# 获取 */
    if (nargs == 0) {
        const char *cnt = getenv("#");
        nargs = cnt ? atoi(cnt) : 0;
        if (nargs > 0) {
            args = malloc(sizeof(char*) * nargs);
            for (int i = 0; i < nargs; i++) {
                char vn[16];
                snprintf(vn, sizeof(vn), "%d", i + 1);
                const char *v = getenv(vn);
                args[i] = (char*)(v ? v : "");
            }
        }
    }

    /* 检查索引范围 */
    if (optind_val > nargs) {
        /* 无更多参数 */
        setenv(varname, "?", 1);
        if (args != &argv[3]) free(args);
        return 1;
    }

    const char *arg = args[optind_val - 1];
    if (!arg || arg[0] != '-' || arg[1] == '\0' || (arg[1] == '-' && arg[2] == '\0')) {
        /* 不是选项或遇到 -- */
        if (arg && arg[1] == '-' && arg[2] == '\0')
            optind_val++;
        setenv(varname, "?", 1);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", optind_val);
        setenv("OPTIND", buf, 1);
        if (args != &argv[3]) free(args);
        return 1;
    }

    /* 获取当前选项字符 */
    static int char_index = 1;  /* 多字符选项中的位置 */
    /* 注意：POSIX getopts 使用 OPTIND，但同一参数中的多字符选项需要内部状态 */
    char opt = arg[char_index];
    if (opt == '\0') {
        /* 当前参数已耗尽 */
        optind_val++;
        char_index = 1;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", optind_val);
        setenv("OPTIND", buf, 1);
        if (args != &argv[3]) free(args);
        /* 递归调用处理下一个 */
        return msh_builtin_getopts(argc, argv);
    }

    /* 在 optstring 中查找 */
    const char *p = strchr(optstring, opt);
    if (!p) {
        /* 未知选项 */
        setenv(varname, "?", 1);
        setenv("OPTARG", "", 1);
        char_index++;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", optind_val);
        setenv("OPTIND", buf, 1);
        if (args != &argv[3]) free(args);
        return 0;
    }

    /* 检查是否需要参数 */
    if (p[1] == ':') {
        /* 选项需要参数 */
        if (arg[char_index + 1] != '\0') {
            /* 参数在当前 arg 的剩余部分 */
            setenv("OPTARG", arg + char_index + 1, 1);
            optind_val++;
            char_index = 1;
        } else {
            /* 参数在下一个 arg */
            optind_val++;
            if (optind_val <= nargs) {
                setenv("OPTARG", args[optind_val - 1], 1);
                optind_val++;
            } else {
                /* 缺少参数 */
                if (optstring[0] == ':') {
                    /* 以 : 开头：返回 : 并设置 OPTARG 为选项字符 */
                    char ob[2] = {opt, '\0'};
                    setenv(varname, ":", 1);
                    setenv("OPTARG", ob, 1);
                } else {
                    fprintf(stderr, "msh: getopts: option requires an argument -- %c\n", opt);
                    setenv(varname, "?", 1);
                }
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", optind_val);
                setenv("OPTIND", buf, 1);
                if (args != &argv[3]) free(args);
                return 2;
            }
            char_index = 1;
        }
    } else {
        /* 不需要参数 */
        unsetenv("OPTARG");
        char_index++;
        /* 如果当前 arg 的选项已耗尽 */
        if (arg[char_index] == '\0') {
            optind_val++;
            char_index = 1;
        }
    }

    /* 设置变量为选项字符 */
    char obuf[2] = {opt, '\0'};
    setenv(varname, obuf, 1);

    /* 更新 OPTIND */
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", optind_val);
    setenv("OPTIND", buf, 1);

    if (args != &argv[3]) free(args);
    return 0;
}

/* === hash 内建（stub） === */
int msh_builtin_hash(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-r")) return 0;
    printf("hash commands table is empty\n");
    return 0;
}
