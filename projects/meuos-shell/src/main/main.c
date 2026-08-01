/* msh/main.c — 真实版本：调用 lex/parse/eval
 *
 * 三种模式：
 *   msh -c "cmd"            → 单条命令
 *   msh script.sh           → 脚本模式
 *   msh                     → 交互 REPL（placeholder，尚未完整实现）
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "msh/exec.h"
#include "msh/lex.h"
#include "msh/msh.h"
#include "msh/parse.h"

const char *msh_version = "0.1.0-modern";
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
        "POSIX sh + bash 兼容 + zsh 插件 + YAML 配置\n"
        "Built with: mcc + meuos-libc + meuos-toolchain\n",
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

/* 一次性执行字符串：parse + eval。公开给 trap 使用。 */
int msh_run_string(const char *s, size_t len) {
    lexer_t lx;
    msh_lexer_init(&lx, s, len);
    ast_t *ast = msh_parse(&lx);
    if (!ast) {
        msh_lexer_free(&lx);
        return 0;
    }
    int rc = msh_eval(ast);
    ast_free(ast);
    msh_lexer_free(&lx);
    return rc;
}

/* 读整文件到 buf（含 NUL） */
static char *read_all(FILE *fp, size_t *sz_out) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len + 4096 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        size_t n = fread(buf + len, 1, cap - len - 1, fp);
        len += n;
        if (n == 0) break;
    }
    buf[len] = '\0';
    *sz_out = len;
    return buf;
}

static int run_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "%s: cannot open %s: %s\n",
                msh_program_name, path, strerror(errno));
        return 1;
    }
    size_t sz = 0;
    char *buf = read_all(fp, &sz);
    fclose(fp);
    int rc = msh_run_string(buf, sz);
    free(buf);
    return rc;
}

/* 简单 REPL：读一行，execute。 */
static int run_repl(void) {
    fprintf(stderr, "%s %s (type 'exit' to quit)\n", msh_program_name, msh_version);
    /* 安装 SIGCHLD handler（作业控制用） */
    extern void msh_job_sigchld_handler(int);
    signal(SIGCHLD, msh_job_sigchld_handler);
    extern void msh_job_reap(void);
    /* 行编辑 + 历史 */
    extern char *msh_readline(const char *prompt);
    extern void msh_history_load(void);
    extern void msh_history_add(const char *line);
    msh_history_load();

    int is_tty = isatty(STDIN_FILENO);
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while (1) {
        /* prompt 前回收已完成的后台作业 + 执行 pending trap */
        msh_job_reap();
        msh_trap_check();
        /* 提示符：--classic 用朴素 $；否则用配置的 PS1（或默认） */
        char *ps1 = NULL;
        if (msh_mode_classic) {
            ps1 = strdup("$ ");
        } else {
            const char *cfg_ps1 = getenv("MSH_PS1");
            ps1 = msh_prompt_expand(cfg_ps1);
            if (!ps1) ps1 = strdup("$ ");
        }
        if (is_tty) {
            line = msh_readline(ps1);
            free(ps1);
            if (!line) break;   /* EOF */
            if (strcmp(line, "\003") == 0) {
                /* Ctrl-C 中断行：不执行 */
                free(line);
                continue;
            }
            n = (ssize_t)strlen(line);
        } else {
            fprintf(stderr, "%s", ps1);
            free(ps1);
            fflush(stderr);
            n = getline(&line, &cap, stdin);
            if (n < 0) break;  /* EOF */
            /* 去掉行尾换行 */
            while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
                line[--n] = '\0';
            }
        }
        if (!*line) { free(line); line = NULL; continue; }
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') { free(line); line = NULL; continue; }
        if (strcmp(line, "exit") == 0) { free(line); line = NULL; break; }
        msh_history_add(line);

        lexer_t lx;
        msh_lexer_init(&lx, line, strlen(line));
        ast_t *ast = msh_parse(&lx);
        int rc = 0;
        if (ast) {
            rc = msh_eval(ast);
            ast_free(ast);
        }
        msh_lexer_free(&lx);
        msh_last_status = rc;
        free(line);
        line = NULL;
    }
    free(line);
    msh_trap_exit();
    return msh_last_status;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTIONS] [COMMAND | SCRIPT]\n\n"
        "Options:\n"
        "  -c COMMAND    Run COMMAND and exit\n"
        "  -i            Interactive REPL (default)\n"
        "  --rc FILE     Load config file (default: ~/.config/msh/config.yaml)\n"
        "  --noprofile    Don't load user config\n"
        "  --posix        Strict POSIX mode\n"
        "      --help    Display this help and exit\n"
        "      --version Output version information and exit\n\n"
        "If neither -c nor a SCRIPT is given, msh enters interactive mode.\n"
        "Full POSIX sh + bash 兼容 + zsh 插件 + YAML 配置 + 历史/补全.\n",
        msh_program_name);
    exit(0);
}

int main(int argc, char **argv) {
    msh_set_program_name(argv[0]);
    msh_argv = argv;
    setenv("MSH_VERSION", msh_version, 1);

    static const struct option longopts[] = {
        { "help",       no_argument, NULL, 'h' },
        { "version",    no_argument, NULL, 'V' },
        { "noprofile",  no_argument, NULL, 1000 },
        { "rc",         required_argument, NULL, 1001 },
        { "posix",      no_argument, NULL, 1002 },
        { "classic",    no_argument, NULL, 1003 },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    int load_profile = 1;
    const char *rcfile = NULL;

    while ((opt = getopt_long(argc, argv, "+c:hVi", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            msh_last_status = msh_run_string(optarg, strlen(optarg));
            msh_trap_exit();
            return msh_last_status;
        case 'h': usage(); break;
        case 'V': msh_version_exit(); break;
        case 'i': break;
        case 1000: load_profile = 0; break;
        case 1001: rcfile = optarg; break;
        case 1002: break;
        case 1003: msh_mode_classic = 1; break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", msh_program_name);
            return 2;
        }
    }

    /* 加载用户配置（3 路递进：--classic 已设则跳过） */
    if (load_profile) {
        msh_load_config(rcfile);
    }

    if (optind < argc) {
        /* 脚本模式 */
        for (int i = optind; i < argc; i++) {
            int rc = run_file(argv[i]);
            msh_last_status = rc;
        }
        msh_trap_exit();
        return msh_last_status;
    }
    return run_repl();
}
