/* libutils/version.c — 版本信息与 program_name 管理 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

/* 版本号集中在此处定义。变更时同步 README.md + AGENTS.md。 */
const char *meuos_utils_version = "0.1.0-skeleton";

const char *program_name = NULL;

void set_program_name(const char *argv0) {
    if (!argv0) {
        program_name = "utils";
        return;
    }
    /* 去掉路径前缀，只保留 basename */
    const char *base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    program_name = base;
}

void print_version(FILE *fp) {
    fprintf(fp, "%s (meuos-utils) %s\n",
            program_name ? program_name : "utils",
            meuos_utils_version);
    fprintf(fp, "Copyright (C) 2026 MeuOS Kit contributors\n");
    fprintf(fp, "License: RFL v1.0\n");
}

void version(void) {
    print_version(stdout);
    exit(0);
}

/* === 一站式初始化 === */
int utils_init(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    /* 扫描 argv 查找 --version（任意位置），自动拦截 */
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--version") == 0)
            version(); /* 不返回 */
    }

    return 1; /* 工具选项从 argv[1] 开始解析 */
}

/* === --help 输出助手 === */
void utils_usage(const char *usage_text) {
    /* 先输出版本行（不含 LICENSE，简洁） */
    printf("%s (meuos-utils) %s\n",
           program_name ? program_name : "utils",
           meuos_utils_version);
    /* 输出工具特定的 usage 文本 */
    if (usage_text)
        fputs(usage_text, stdout);
    exit(0);
}

void utils_die_usage(const char *usage_text) {
    fprintf(stderr, "%s (meuos-utils) %s\n",
            program_name ? program_name : "utils",
            meuos_utils_version);
    if (usage_text)
        fputs(usage_text, stderr);
    exit(2);
}

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", program_name ? program_name : "utils");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}
