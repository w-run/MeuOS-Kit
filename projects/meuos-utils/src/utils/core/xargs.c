/* xargs - 从 stdin 读参数分组执行命令
 *
 * 支持选项：
 *   -n N          每组最多 N 个参数
 *   -I REPL      替换 REPL（如 {}) 为输入项
 *   -0, --null   NUL 字节分隔而非换行
 *   -d DELIM     自定义分隔符
 *   -t, --verbose  执行前打印命令到 stderr
 *   --classic    POSIX 风格
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... COMMAND [INITIAL-ARGS]...\n"
        "Run COMMAND with arguments read from standard input.\n\n"
        "  -n N           use at most N arguments per command line\n"
        "  -I REPL        replace REPL with input items\n"
        "  -0, --null     items are separated by NUL, not whitespace\n"
        "  -d DELIM       custom delimiter character\n"
        "  -t, --verbose  print command before executing\n"
        "      --classic  POSIX style\n"
        "      --help     display this help and exit\n"
        "      --version  output version information and exit\n",
        program_name);
    exit(0);
}

/* 执行 argv[0..argc-1]（最后一个 NULL） */
static int run_cmd(char **argv, int verbose) {
    if (verbose) {
        fprintf(stderr, "%s", argv[0]);
        for (int i = 1; argv[i]; i++) fprintf(stderr, " %s", argv[i]);
        fputc('\n', stderr);
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror(program_name);
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s: %s\n", program_name, argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int n_max = 0;
    const char *repl = NULL;
    int null_sep = 0;
    char delim = '\n';
    int verbose = 0;

    static const struct utils_option longopts[] = {
        { "null",    no_argument,       NULL, '0' },
        { "verbose", no_argument,       NULL, 't' },
        { "classic", no_argument,       NULL, 1000 },
        { "help",    no_argument,       NULL, 'h' },
        { "version", no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "n:I:0d:thV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'n': n_max = atoi(utils_optarg); break;
        case 'I': repl = utils_optarg; break;
        case '0': null_sep = 1; break;
        case 'd': delim = utils_optarg[0]; break;
        case 't': verbose = 1; break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    /* 默认命令是 echo */
    int init_argc = argc - utils_optind;
    char **init_argv = argv + utils_optind;
    if (init_argc == 0) {
        static char *def[] = { "echo", NULL };
        init_argv = def;
        init_argc = 1;
    }

    /* 读取输入行 */
    char **items = NULL;
    int n_items = 0, cap = 0;
    char sep = null_sep ? '\0' : delim;
    char buf[65536];
    size_t blen = 0;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (c == sep) {
            buf[blen] = '\0';
            if (n_items == cap) {
                cap = cap ? cap * 2 : 16;
                items = xrealloc(items, (size_t)cap * sizeof(char *));
            }
            items[n_items++] = xstrdup(buf);
            blen = 0;
        } else {
            if (blen + 1 < sizeof(buf)) buf[blen++] = (char)c;
        }
    }
    if (blen > 0) {
        buf[blen] = '\0';
        if (n_items == cap) {
            cap = cap ? cap * 2 : 16;
            items = xrealloc(items, (size_t)cap * sizeof(char *));
        }
        items[n_items++] = xstrdup(buf);
    }

    /* 构造并执行 */
    int last_rc = 0;
    if (repl) {
        /* -I 模式：每行执行一次，替换 REPL */
        for (int i = 0; i < n_items; i++) {
            char **av = xmalloc((size_t)(init_argc + 1) * sizeof(char *));
            for (int j = 0; j < init_argc; j++) {
                if (strstr(init_argv[j], repl)) {
                    /* 替换所有出现的 repl */
                    size_t rl = strlen(repl);
                    size_t il = strlen(items[i]);
                    /* 简化：假设每个 init argv 中 repl 最多出现一次 */
                    char *p = strstr(init_argv[j], repl);
                    size_t prefix = (size_t)(p - init_argv[j]);
                    size_t suffix = strlen(p + rl);
                    char *r = xmalloc(prefix + il + suffix + 1);
                    memcpy(r, init_argv[j], prefix);
                    memcpy(r + prefix, items[i], il);
                    memcpy(r + prefix + il, p + rl, suffix);
                    r[prefix + il + suffix] = '\0';
                    av[j] = r;
                } else {
                    av[j] = init_argv[j];
                }
            }
            av[init_argc] = NULL;
            int rc = run_cmd(av, verbose);
            if (rc > last_rc) last_rc = rc;
            /* free 替换的 */
            for (int j = 0; j < init_argc; j++) {
                if (strstr(init_argv[j], repl)) free(av[j]);
            }
            free(av);
        }
    } else {
        /* -n 模式：分组执行 */
        int group = n_max > 0 ? n_max : 256;
        for (int i = 0; i < n_items; i += group) {
            int this_n = n_items - i;
            if (this_n > group) this_n = group;
            char **av = xmalloc((size_t)(init_argc + this_n + 1) * sizeof(char *));
            for (int j = 0; j < init_argc; j++) av[j] = init_argv[j];
            for (int j = 0; j < this_n; j++) av[init_argc + j] = items[i + j];
            av[init_argc + this_n] = NULL;
            int rc = run_cmd(av, verbose);
            if (rc > last_rc) last_rc = rc;
            free(av);
        }
    }

    for (int i = 0; i < n_items; i++) free(items[i]);
    free(items);
    return last_rc;
}
