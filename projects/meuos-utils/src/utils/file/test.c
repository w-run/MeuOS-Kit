/* test / [ — POSIX test 命令
 *
 * 用法：test expr  OR  [ expr ]
 * 退出码：表达式为真返回 0，假返回 1，错误返回 2
 *
 * 支持：
 *   字符串：-n string  -z string  s1 = s2  s1 != s2
 *   整数：n1 -eq n2  -ne  -gt  -ge  -lt  -le
 *   文件：-e  -f  -d  -r  -w  -x  -s  -h/-L  -b  -c  -p  -S  -t fd
 *   组合：!  expr1 -a expr2  expr1 -o expr2  ( expr )
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "meuos/utils.h"

static int pos = 0;
static int argc_global;
static char **argv_global;

static void skip_pos(int n) { pos += n; }

static int test_string(const char *s, const char *op, const char *t) {
    if (!strcmp(op, "=")) return strcmp(s, t) == 0;
    if (!strcmp(op, "!=")) return strcmp(s, t) != 0;
    if (!strcmp(op, "<")) return strcmp(s, t) < 0;
    if (!strcmp(op, ">")) return strcmp(s, t) > 0;
    fprintf(stderr, "test: unknown operator: %s\n", op);
    exit(2);
}

static int test_int(const char *s, const char *op, const char *t) {
    char *e1, *e2;
    long a = strtol(s, &e1, 10);
    long b = strtol(t, &e2, 10);
    if (*e1 || *e2) {
        fprintf(stderr, "test: integer expected: %s%s%s\n",
                s, op, t);
        exit(2);
    }
    if (!strcmp(op, "-eq")) return a == b;
    if (!strcmp(op, "-ne")) return a != b;
    if (!strcmp(op, "-gt")) return a > b;
    if (!strcmp(op, "-ge")) return a >= b;
    if (!strcmp(op, "-lt")) return a < b;
    if (!strcmp(op, "-le")) return a <= b;
    fprintf(stderr, "test: unknown operator: %s\n", op);
    exit(2);
}

static int test_file(const char *path, char op) {
    struct stat st;
    switch (op) {
    case 'e': return access(path, F_OK) == 0;
    case 'f': return stat(path, &st) == 0 && S_ISREG(st.st_mode);
    case 'd': return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    case 'b': return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
    case 'c': return stat(path, &st) == 0 && S_ISCHR(st.st_mode);
    case 'p': return stat(path, &st) == 0 && S_ISFIFO(st.st_mode);
    case 'S': return stat(path, &st) == 0 && S_ISSOCK(st.st_mode);
    case 'L': case 'h': return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
    case 'r': return access(path, R_OK) == 0;
    case 'w': return access(path, W_OK) == 0;
    case 'x': return access(path, X_OK) == 0;
    case 's': return stat(path, &st) == 0 && st.st_size > 0;
    case 'u': return stat(path, &st) == 0 && (st.st_mode & S_ISUID);
    case 'g': return stat(path, &st) == 0 && (st.st_mode & S_ISGID);
    case 'k': return stat(path, &st) == 0 && (st.st_mode & S_ISVTX);
    case 't': {
        int fd = atoi(path);
        return isatty(fd);
    }
    default:
        fprintf(stderr, "test: unknown file test: -%c\n", op);
        exit(2);
    }
}

/* 递归下降解析器 */
static int parse_expr(void);  /* 处理 -o (OR) */
static int parse_and(void);   /* 处理 -a (AND) */
static int parse_not(void);   /* 处理 ! */
static int parse_primary(void); /* 处理 ( expr ) 或基本测试 */

static int parse_expr(void) {
    int val = parse_and();
    while (pos < argc_global && !strcmp(argv_global[pos], "-o")) {
        pos++;
        int right = parse_and();
        val = val || right;
    }
    return val;
}

static int parse_and(void) {
    int val = parse_not();
    while (pos < argc_global && !strcmp(argv_global[pos], "-a")) {
        pos++;
        int right = parse_not();
        val = val && right;
    }
    return val;
}

static int parse_not(void) {
    if (pos < argc_global && !strcmp(argv_global[pos], "!")) {
        pos++;
        return !parse_not();
    }
    return parse_primary();
}

static int parse_primary(void) {
    if (pos >= argc_global) return 0;

    /* ( expr ) */
    if (!strcmp(argv_global[pos], "(")) {
        pos++;
        int val = parse_expr();
        if (pos < argc_global && !strcmp(argv_global[pos], ")")) pos++;
        else { fprintf(stderr, "test: missing )"); exit(2); }
        return val;
    }

    /* ) */
    if (!strcmp(argv_global[pos], ")")) {
        fprintf(stderr, "test: unexpected )");
        exit(2);
    }

    /* -n string */
    if (!strcmp(argv_global[pos], "-n")) {
        pos++;
        if (pos >= argc_global) { fprintf(stderr, "test: -n needs arg"); exit(2); }
        const char *s = argv_global[pos++];
        return s && *s;
    }

    /* -z string */
    if (!strcmp(argv_global[pos], "-z")) {
        pos++;
        if (pos >= argc_global) { fprintf(stderr, "test: -z needs arg"); exit(2); }
        const char *s = argv_global[pos++];
        return !(s && *s);
    }

    /* -t fd */
    if (!strcmp(argv_global[pos], "-t")) {
        pos++;
        if (pos >= argc_global) { /* -t 无参数 = 1 (stdin) */ return isatty(1); }
        return test_file(argv_global[pos++], 't');
    }

    /* 单字符文件测试 -X file */
    if (argv_global[pos][0] == '-' && argv_global[pos][1] != '\0' && argv_global[pos][2] == '\0') {
        char op = argv_global[pos][1];
        pos++;
        if (pos >= argc_global) { fprintf(stderr, "test: -%c needs arg", op); exit(2); }
        return test_file(argv_global[pos++], op);
    }

    /* 二元操作符：arg1 op arg2 */
    if (pos + 2 < argc_global) {
        const char *s1 = argv_global[pos];
        const char *op = argv_global[pos + 1];
        const char *s2 = argv_global[pos + 2];

        /* 检查 op 是否是操作符 */
        if (!strcmp(op, "=") || !strcmp(op, "!=") || !strcmp(op, "<") || !strcmp(op, ">")) {
            pos += 3;
            return test_string(s1, op, s2);
        }
        if (!strcmp(op, "-eq") || !strcmp(op, "-ne") || !strcmp(op, "-gt") ||
            !strcmp(op, "-ge") || !strcmp(op, "-lt") || !strcmp(op, "-le")) {
            pos += 3;
            return test_int(s1, op, s2);
        }
    }

    /* 一元：string（非空字符串为真）*/
    const char *s = argv_global[pos++];
    return s && *s;
}

static void usage(void) {
    fprintf(stderr, "Usage: test EXPRESSION\n   or: [ EXPRESSION ]\n");
    exit(2);
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--version"))) {
        printf("test %s\n", version);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--help"))) usage();

    /* [ 命令需要匹配的 ] */
    int is_bracket = 0;
    const char *name = argv[0];
    const char *base = strrchr(name, '/');
    name = base ? base + 1 : name;
    if (!strcmp(name, "[")) {
        is_bracket = 1;
        /* 最后一个参数必须是 ] */
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            fprintf(stderr, "test: missing ]'\n");
            exit(2);
        }
        argc--;  /* 去掉 ] */
    }

    argc_global = argc;
    argv_global = argv;

    if (argc < 2) return 1;  /* 无表达式为假 */

    pos = 1;
    int result = parse_expr();

    return result ? 0 : 1;
}
