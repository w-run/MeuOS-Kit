/* msh/main.c — msh 入口与 argv 解析
 *
 * 骨架阶段职责：
 *   - 解析 --version / --help / -c COMMAND / script_file
 *   - 设置 program_name
 *   - 派发到交互/脚本/单命令三种模式
 *
 * 三种模式：
 *   1. argv[0] 是 msh, 无 -c/无 script_file：交互模式
 *   2. msh -c "echo hello"：执行单条命令并退出
 *   3. msh script.sh：执行脚本并退出
 *
 * 骨架实现"足够用来跑：
 *   msh -c "echo hello"     → 输出 hello（即使没实现 lex/parse）
 * 的最小子集：分割第一行 token + 找 PATH + execvp。
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE  /* getline 需要 */
#define _BSD_SOURCE      /* strdup 在 stdlib.h */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msh/msh.h"

const char *msh_version = "0.1.0-skeleton";
const char *msh_license = "RFL v1.0";
const char *msh_program_name = "msh";

void msh_set_program_name(const char *argv0) {
    if (!argv0) { msh_program_name = "msh"; return; }
    const char *base = strrchr(argv0, '/');
    msh_program_name = base ? base + 1 : argv0;
}

void msh_version_print(FILE *fp) {
    fprintf(fp,
        "%s (MeuOS Shell) %s\n"
        "Copyright (C) 2026 MeuOS Kit contributors\n"
        "License: %s\n"
        "POSIX sh subset for MeuOS Next, mcc-compiled.\n",
        msh_program_name, msh_version, msh_license);
}

void msh_version_exit(void) {
    msh_version_print(stdout);
    exit(0);
}

void msh_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", msh_program_name);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

/* --- 简单命令执行（骨架） --- */

/* 用空白分割字符串到 argv。不处理引号/转义（骨架）。 */
static char **split_args(char *line, int *outc) {
    int cap = 8;
    char **argv = malloc(sizeof(char *) * cap);
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= cap) {
            cap *= 2;
            argv = realloc(argv, sizeof(char *) * cap);
        }
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = NULL;
    *outc = argc;
    return argv;
}

/* 执行单个命令（fork+execvp）。返回退出码。 */
static int run_simple(char *line) {
    int argc = 0;
    char **argv = split_args(line, &argc);
    if (argc == 0) {
        free(argv);
        return 0;
    }

    /* 处理 PATH 中的命令：fork + execvp */
    pid_t pid = fork();
    if (pid < 0) {
        msh_die("fork: %s", strerror(errno));
    }
    if (pid == 0) {
        /* 子进程 */
        execvp(argv[0], argv);
        /* execvp 失败 */
        fprintf(stderr, "%s: %s: %s\n", msh_program_name, argv[0], strerror(errno));
        _exit(127);
    }
    /* 父进程等待 */
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        free(argv);
        return -1;
    }
    free(argv);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}

/* 读一个文件，逐行执行。
 * 骨架版本：不解析任何 shell 语法，仅按行直接交给 run_simple。 */
static int run_script(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        msh_die("cannot open %s: %s", path, strerror(errno));
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int last_status = 0;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        /* 去掉行尾换行 */
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[--n] = '\0';
        }
        /* 跳过空行和注释 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;
        last_status = run_simple(p);
    }
    free(line);
    fclose(fp);
    return last_status;
}

/* 交互模式骨架：readline 简化（getline 替换） */
static int run_interactive(void) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    fprintf(stderr, "%s %s (type 'exit' to quit)\n", msh_program_name, msh_version);
    int last_status = 0;
    while (1) {
        fprintf(stderr, "$ ");
        fflush(stderr);
        n = getline(&line, &cap, stdin);
        if (n < 0) break;  /* EOF (Ctrl-D) */
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[--n] = '\0';
        }
        if (strcmp(line, "exit") == 0) break;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != '#') {
            last_status = run_simple(p);
        }
    }
    free(line);
    return last_status;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTIONS] [COMMAND | SCRIPT]\n\n"
        "Options:\n"
        "  -c COMMAND    Run COMMAND and exit\n"
        "  --help        Display this help and exit\n"
        "  --version     Output version information and exit\n\n"
        "If neither -c nor a SCRIPT is given, msh enters interactive mode.\n\n"
        "Status: skeleton stage — only simple commands via fork/execvp.\n"
        "Full POSIX sh subset under development (P6 stage).\n",
        msh_program_name);
    exit(0);
}

int main(int argc, char **argv) {
    msh_set_program_name(argv[0]);

    static const struct option longopts[] = {
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL,      0,           NULL,  0  },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c': {
            /* 单命令模式：执行后退出 */
            int rc = run_simple(optarg);
            return rc;
        }
        case 'h': usage(); break;
        case 'V': msh_version_exit(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n",
                    msh_program_name);
            return 2;
        }
    }

    if (optind < argc) {
        /* 脚本模式：argv[optind] 是脚本文件 */
        return run_script(argv[optind]);
    } else {
        /* 交互模式 */
        return run_interactive();
    }
}
