/* kill — 发送信号给进程
 * 用法：kill [-SIGNAL | -s SIGNAL] PID...
 *       kill -l [SIGNAL]
 * 选项：-l 列出信号, -s SIGNAL 指定信号, -L 列出信号（别名）
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

#include "meuos/utils.h"

static const struct { const char *name; int sig; } sigtab[] = {
    {"HUP", SIGHUP}, {"INT", SIGINT}, {"QUIT", SIGQUIT}, {"ILL", SIGILL},
    {"ABRT", SIGABRT}, {"FPE", SIGFPE}, {"KILL", SIGKILL}, {"SEGV", SIGSEGV},
    {"TERM", SIGTERM}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"PIPE", SIGPIPE},
    {"ALRM", SIGALRM}, {"CHLD", SIGCHLD}, {"CONT", SIGCONT}, {"STOP", SIGSTOP},
    {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN}, {"TTOU", SIGTTOU}, {"BUS", SIGBUS},
    {0, 0}
};

static int sig_from_name(const char *s) {
    if (strncmp(s, "SIG", 3) == 0) s += 3;
    for (int i = 0; sigtab[i].name; i++)
        if (!strcasecmp(sigtab[i].name, s)) return sigtab[i].sig;
    return -1;
}

static const char *sig_to_name(int sig) {
    for (int i = 0; sigtab[i].name; i++)
        if (sigtab[i].sig == sig) return sigtab[i].name;
    return NULL;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    int sig = SIGTERM;

    if (argi < argc && !strcmp(argv[argi], "--help")) {
        printf("Usage: kill [-s SIGNAL | -SIGNAL] PID...\n  or:  kill -l [SIGNAL]\n");
        return 0;
    }
    if (argi < argc && !strcmp(argv[argi], "-l")) {
        if (argc > 2) {
            int s = atoi(argv[2]);
            if (s <= 0) s = sig_from_name(argv[2]);
            const char *n = sig_to_name(s);
            if (n) printf("%s\n", n);
            else printf("%d\n", s);
        } else {
            for (int i = 0; sigtab[i].name; i++)
                printf("%2d) %-6s%c", sigtab[i].sig, sigtab[i].name,
                       (i % 8 == 7) ? '\n' : ' ');
            putchar('\n');
        }
        return 0;
    }
    if (argi < argc && !strcmp(argv[argi], "-L")) {
        for (int i = 0; sigtab[i].name; i++)
            printf("%2d) %-6s%c", sigtab[i].sig, sigtab[i].name,
                   (i % 8 == 7) ? '\n' : ' ');
        putchar('\n');
        return 0;
    }
    if (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "-s")) { argi++; if (argi >= argc) { fprintf(stderr, "kill: -s: option requires an argument\n"); return 2; } sig = sig_from_name(argv[argi]); argi++; }
        else if (argv[argi][1] >= '0' && argv[argi][1] <= '9') { sig = atoi(argv[argi] + 1); argi++; }
        else { sig = sig_from_name(argv[argi] + 1); argi++; }
        if (sig < 0) { fprintf(stderr, "kill: unknown signal\n"); return 2; }
    }
    if (argi >= argc) { fprintf(stderr, "kill: missing PID\n"); return 2; }
    int rc = 0;
    for (int i = argi; i < argc; i++) {
        const char *pidstr = argv[i];
        if (*pidstr == '-') pidstr++;
        if (!isdigit((unsigned char)*pidstr)) {
            fprintf(stderr, "kill: %s: arguments must be process or job IDs\n", argv[i]);
            rc = 1; continue;
        }
        pid_t pid = (pid_t)atoi(pidstr);
        if (kill(pid, sig) < 0) {
            fprintf(stderr, "kill: (%d) - %s\n", (int)pid, strerror(errno));
            rc = 1;
        }
    }
    return rc;
}
