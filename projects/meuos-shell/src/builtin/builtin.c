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

/* === let 内建：算术表达式求值 ===
 *
 * 递归下降算术表达式解析器，支持：
 *   - 基本运算: + - * / %
 *   - 幂运算: ** (右结合)
 *   - 位运算: & | ^ ~ << >>
 *   - 逻辑: && || !
 *   - 比较: < > <= >= == !=
 *   - 赋值: = += -= *= /= %= &= |= ^= <<= >>=
 *   - 前/后缀 ++ --
 *   - 三元: ?:
 *   - 括号: ( )
 *   - 变量引用: 变量名直接展开为 getenv(变量名) 的整数值
 *   - 十六进制 (0x) 和八进制 (0) 整数字面量
 *
 * 返回值: 表达式的求值结果 (long)
 * 错误时设置 *err=1
 */
static long msh_arith_eval(const char *expr, int *err);

/* let 内建命令入口 */
int msh_builtin_let(int argc, char **argv) {
    if (argc < 2) return 1;

    long result = 0;
    int err = 0;

    for (int i = 1; i < argc; i++) {
        result = msh_arith_eval(argv[i], &err);
        if (err) {
            fprintf(stderr, "msh: let: %s: expression error\n", argv[i]);
            return 1;
        }
    }

    /* let 返回 0 如果结果非零，1 如果结果为零 */
    return (result != 0) ? 0 : 1;
}

/* ---- 递归下降算术解析器 ---- */

typedef struct {
    const char *p;    /* 当前位置 */
    int err;          /* 错误标志 */
} arith_ctx;

static void arith_skip_ws(arith_ctx *c) {
    while (*c->p == ' ' || *c->p == '\t') c->p++;
}

static long arith_parse_expr(arith_ctx *c);

/* 解析一个"因子" (primary) */
static long arith_parse_primary(arith_ctx *c) {
    arith_skip_ws(c);

    /* 括号 */
    if (*c->p == '(') {
        c->p++;  /* skip '(' */
        long val = arith_parse_expr(c);
        arith_skip_ws(c);
        if (*c->p != ')') { c->err = 1; return 0; }
        c->p++;  /* skip ')' */
        return val;
    }

    /* 前缀 ++ -- */
    if ((c->p[0] == '+' && c->p[1] == '+') ||
        (c->p[0] == '-' && c->p[1] == '-')) {
        int is_inc = (c->p[0] == '+');
        c->p += 2;
        arith_skip_ws(c);

        /* 读取变量名 */
        const char *start = c->p;
        if (!((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') || *c->p == '_')) {
            c->err = 1; return 0;
        }
        while ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') ||
               (*c->p >= '0' && *c->p <= '9') || *c->p == '_')
            c->p++;

        size_t namelen = (size_t)(c->p - start);
        char namebuf[256];
        if (namelen >= sizeof(namebuf)) namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, start, namelen);
        namebuf[namelen] = '\0';

        const char *val = getenv(namebuf);
        long v = val ? atol(val) : 0;
        v += is_inc ? 1 : -1;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", v);
        setenv(namebuf, buf, 1);
        return v;
    }

    /* 一元运算: - + ! ~ */
    if (*c->p == '-') { c->p++; return -arith_parse_primary(c); }
    if (*c->p == '+') { c->p++; return arith_parse_primary(c); }
    if (*c->p == '!') { c->p++; return !arith_parse_primary(c); }
    if (*c->p == '~') { c->p++; return ~arith_parse_primary(c); }

    /* 数字字面量: 0x 十六进制, 0 八进制, 十进制 */
    if (*c->p >= '0' && *c->p <= '9') {
        char *end;
        long val;
        if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X'))
            val = strtol(c->p, &end, 16);
        else if (c->p[0] == '0' && c->p[1] >= '0' && c->p[1] <= '9')
            val = strtol(c->p, &end, 8);
        else
            val = strtol(c->p, &end, 10);
        c->p = end;
        return val;
    }

    /* 变量名 */
    if ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') || *c->p == '_') {
        const char *start = c->p;
        while ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') ||
               (*c->p >= '0' && *c->p <= '9') || *c->p == '_')
            c->p++;
        size_t namelen = (size_t)(c->p - start);
        char namebuf[256];
        if (namelen >= sizeof(namebuf)) namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, start, namelen);
        namebuf[namelen] = '\0';
        const char *val = getenv(namebuf);
        return val ? atol(val) : 0;
    }

    c->err = 1;
    return 0;
}

/* 后缀 ++ -- */
static long arith_parse_postfix(arith_ctx *c) {
    long val = arith_parse_primary(c);
    arith_skip_ws(c);

    /* 后缀 ++ / -- */
    if (c->p[0] == '+' && c->p[1] == '+') {
        c->p += 2;
        /* 需要原始变量名，但 arith_parse_primary 已经消费了它。
         * 简化：回退两个字符找到变量名... 实际上这比较复杂。
         * 让我们重新解析：在调用前保存上下文。
         * 为简化实现，后缀 ++/-- 仅对简单变量工作。
         * 重新实现：重新走一遍。*/
        /* 回退并提取变量名 */
        const char *save = c->p;
        c->p -= 2;  /* 回退到 ++ */
        /* 再回退到变量名 */
        c->p--;
        const char *name_end = c->p + 1;
        while (c->p > save - 256 &&
               ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') ||
                (*c->p >= '0' && *c->p <= '9') || *c->p == '_'))
            c->p--;
        c->p++;  /* 跳到第一个有效字符 */
        size_t namelen = (size_t)(name_end - c->p);
        char namebuf[256];
        if (namelen >= sizeof(namebuf)) namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, c->p, namelen);
        namebuf[namelen] = '\0';
        /* 设置新值 */
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", val + 1);
        setenv(namebuf, buf, 1);
        /* 恢复位置到 ++ 之后 */
        c->p = save;
        return val;  /* 后缀返回旧值 */
    }
    if (c->p[0] == '-' && c->p[1] == '-') {
        c->p += 2;
        const char *save = c->p;
        c->p -= 2;
        c->p--;
        const char *name_end = c->p + 1;
        while (c->p > save - 256 &&
               ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') ||
                (*c->p >= '0' && *c->p <= '9') || *c->p == '_'))
            c->p--;
        c->p++;
        size_t namelen = (size_t)(name_end - c->p);
        char namebuf[256];
        if (namelen >= sizeof(namebuf)) namelen = sizeof(namebuf) - 1;
        memcpy(namebuf, c->p, namelen);
        namebuf[namelen] = '\0';
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", val - 1);
        setenv(namebuf, buf, 1);
        c->p = save;
        return val;
    }
    return val;
}

/* 幂运算: ** (右结合) */
static long arith_parse_power(arith_ctx *c) {
    long base = arith_parse_postfix(c);
    arith_skip_ws(c);
    if (c->p[0] == '*' && c->p[1] == '*') {
        c->p += 2;
        long exp = arith_parse_power(c);  /* 右结合 */
        long result = 1;
        if (exp < 0) { c->err = 1; return 0; }
        for (long i = 0; i < exp; i++)
            result *= base;
        return result;
    }
    return base;
}

/* 乘/除/模 */
static long arith_parse_mul(arith_ctx *c) {
    long left = arith_parse_power(c);
    for (;;) {
        arith_skip_ws(c);
        if (*c->p == '*') { c->p++; left *= arith_parse_power(c); continue; }
        if (*c->p == '/') { c->p++; long r = arith_parse_power(c); if (r == 0) { c->err = 1; return 0; } left /= r; continue; }
        if (*c->p == '%') { c->p++; long r = arith_parse_power(c); if (r == 0) { c->err = 1; return 0; } left %= r; continue; }
        break;
    }
    return left;
}

/* 加/减 */
static long arith_parse_add(arith_ctx *c) {
    long left = arith_parse_mul(c);
    for (;;) {
        arith_skip_ws(c);
        if (*c->p == '+' && c->p[1] != '+') { c->p++; left += arith_parse_mul(c); continue; }
        if (*c->p == '-' && c->p[1] != '-') { c->p++; left -= arith_parse_mul(c); continue; }
        break;
    }
    return left;
}

/* 移位: << >> */
static long arith_parse_shift(arith_ctx *c) {
    long left = arith_parse_add(c);
    for (;;) {
        arith_skip_ws(c);
        if (c->p[0] == '<' && c->p[1] == '<') { c->p += 2; left <<= arith_parse_add(c); continue; }
        if (c->p[0] == '>' && c->p[1] == '>') { c->p += 2; left >>= arith_parse_add(c); continue; }
        break;
    }
    return left;
}

/* 关系: < > <= >= */
static long arith_parse_rel(arith_ctx *c) {
    long left = arith_parse_shift(c);
    for (;;) {
        arith_skip_ws(c);
        if (c->p[0] == '<' && c->p[1] == '=') { c->p += 2; left = (left <= arith_parse_shift(c)); continue; }
        if (c->p[0] == '>' && c->p[1] == '=') { c->p += 2; left = (left >= arith_parse_shift(c)); continue; }
        if (c->p[0] == '<' && c->p[1] != '<') { c->p++; left = (left < arith_parse_shift(c)); continue; }
        if (c->p[0] == '>' && c->p[1] != '>') { c->p++; left = (left > arith_parse_shift(c)); continue; }
        break;
    }
    return left;
}

/* 相等: == != */
static long arith_parse_eq(arith_ctx *c) {
    long left = arith_parse_rel(c);
    for (;;) {
        arith_skip_ws(c);
        if (c->p[0] == '=' && c->p[1] == '=') { c->p += 2; left = (left == arith_parse_rel(c)); continue; }
        if (c->p[0] == '!' && c->p[1] == '=') { c->p += 2; left = (left != arith_parse_rel(c)); continue; }
        break;
    }
    return left;
}

/* 按位与: & */
static long arith_parse_band(arith_ctx *c) {
    long left = arith_parse_eq(c);
    for (;;) {
        arith_skip_ws(c);
        if (*c->p == '&' && c->p[1] != '&') { c->p++; left &= arith_parse_eq(c); continue; }
        break;
    }
    return left;
}

/* 按位异或: ^ */
static long arith_parse_bxor(arith_ctx *c) {
    long left = arith_parse_band(c);
    for (;;) {
        arith_skip_ws(c);
        if (*c->p == '^') { c->p++; left ^= arith_parse_band(c); continue; }
        break;
    }
    return left;
}

/* 按位或: | */
static long arith_parse_bor(arith_ctx *c) {
    long left = arith_parse_bxor(c);
    for (;;) {
        arith_skip_ws(c);
        if (*c->p == '|' && c->p[1] != '|') { c->p++; left |= arith_parse_bxor(c); continue; }
        break;
    }
    return left;
}

/* 逻辑与: && */
static long arith_parse_land(arith_ctx *c) {
    long left = arith_parse_bor(c);
    arith_skip_ws(c);
    if (c->p[0] == '&' && c->p[1] == '&') {
        c->p += 2;
        long right = arith_parse_land(c);
        return (left && right) ? 1 : 0;
    }
    return left;
}

/* 逻辑或: || */
static long arith_parse_lor(arith_ctx *c) {
    long left = arith_parse_land(c);
    arith_skip_ws(c);
    if (c->p[0] == '|' && c->p[1] == '|') {
        c->p += 2;
        long right = arith_parse_lor(c);
        return (left || right) ? 1 : 0;
    }
    return left;
}

/* 三元: ?: */
static long arith_parse_ternary(arith_ctx *c) {
    long cond = arith_parse_lor(c);
    arith_skip_ws(c);
    if (*c->p == '?') {
        c->p++;
        long true_val = arith_parse_expr(c);
        arith_skip_ws(c);
        if (*c->p != ':') { c->err = 1; return 0; }
        c->p++;
        long false_val = arith_parse_ternary(c);
        return cond ? true_val : false_val;
    }
    return cond;
}

/* 赋值: = += -= *= /= %= &= |= ^= <<= >>=
 * 需要左侧是变量名，因此特殊处理 */
static long arith_parse_assign(arith_ctx *c) {
    /* 先尝试解析左侧作为三元表达式 */
    const char *save = c->p;
    /* 尝试读取变量名 */
    arith_skip_ws(c);
    const char *name_start = c->p;
    if ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') || *c->p == '_') {
        while ((*c->p >= 'a' && *c->p <= 'z') || (*c->p >= 'A' && *c->p <= 'Z') ||
               (*c->p >= '0' && *c->p <= '9') || *c->p == '_')
            c->p++;
        size_t namelen = (size_t)(c->p - name_start);
        arith_skip_ws(c);

        /* 检查是否是赋值运算符 */
        int op = 0;
        if (c->p[0] == '=' && c->p[1] != '=') op = 1;        /* = */
        else if (c->p[0] == '+' && c->p[1] == '=' && c->p[2] != '=') op = 2;  /* += */
        else if (c->p[0] == '-' && c->p[1] == '=' && c->p[2] != '=') op = 3;  /* -= */
        else if (c->p[0] == '*' && c->p[1] == '=' && c->p[2] != '*' && c->p[2] != '=') op = 4;  /* *= */
        else if (c->p[0] == '/' && c->p[1] == '=') op = 5;    /* /= */
        else if (c->p[0] == '%' && c->p[1] == '=') op = 6;   /* %= */
        else if (c->p[0] == '&' && c->p[1] == '=' && c->p[2] != '&') op = 7;  /* &= */
        else if (c->p[0] == '|' && c->p[1] == '=' && c->p[2] != '|') op = 8;  /* |= */
        else if (c->p[0] == '^' && c->p[1] == '=') op = 9;   /* ^= */
        else if (c->p[0] == '<' && c->p[1] == '<' && c->p[2] == '=') op = 10; /* <<= */
        else if (c->p[0] == '>' && c->p[1] == '>' && c->p[2] == '=') op = 11; /* >>= */

        if (op > 0) {
            /* 消费赋值运算符 */
            if (op == 1) c->p += 1;
            else if (op <= 9) c->p += 2;
            else c->p += 3;  /* <<= >>= */

            char namebuf[256];
            if (namelen >= sizeof(namebuf)) namelen = sizeof(namebuf) - 1;
            memcpy(namebuf, name_start, namelen);
            namebuf[namelen] = '\0';

            long rhs = arith_parse_assign(c);  /* 右结合 */
            if (c->err) return 0;

            long old_val = 0;
            const char *old = getenv(namebuf);
            if (old) old_val = atol(old);

            long new_val;
            switch (op) {
                case 1: new_val = rhs; break;
                case 2: new_val = old_val + rhs; break;
                case 3: new_val = old_val - rhs; break;
                case 4: new_val = old_val * rhs; break;
                case 5: if (rhs == 0) { c->err = 1; return 0; } new_val = old_val / rhs; break;
                case 6: if (rhs == 0) { c->err = 1; return 0; } new_val = old_val % rhs; break;
                case 7: new_val = old_val & rhs; break;
                case 8: new_val = old_val | rhs; break;
                case 9: new_val = old_val ^ rhs; break;
                case 10: new_val = old_val << rhs; break;
                case 11: new_val = old_val >> rhs; break;
                default: new_val = rhs; break;
            }

            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", new_val);
            setenv(namebuf, buf, 1);
            return new_val;
        }
    }

    /* 不是赋值，回退并解析为三元表达式 */
    c->p = save;
    return arith_parse_ternary(c);
}

/* 表达式入口 */
static long arith_parse_expr(arith_ctx *c) {
    return arith_parse_assign(c);
}

/* 公开入口 */
static long msh_arith_eval(const char *expr, int *err) {
    arith_ctx ctx = { .p = expr, .err = 0 };
    long result = arith_parse_expr(&ctx);
    arith_skip_ws(&ctx);
    if (*ctx.p != '\0') ctx.err = 1;
    *err = ctx.err;
    return result;
}
