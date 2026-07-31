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

void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", program_name ? program_name : "utils");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}
