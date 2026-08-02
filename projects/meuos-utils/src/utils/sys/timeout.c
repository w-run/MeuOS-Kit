/* timeout — 在指定时间后杀死命令
 * 用法：timeout [-s SIGNAL] DURATION COMMAND [ARG]...
 * 选项：-s SIGNAL 使用的信号(默认 TERM)
 * DURATION 支持复合时长 (1h30m) 和冒号格式 (1:30:00)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "meuos/utils.h"

static int sig_from_name(const char *s) {
    if (strncmp(s, "SIG", 3) == 0) s += 3;
    struct { const char *n; int s; } tab[] = {
        {"TERM", SIGTERM}, {"HUP", SIGHUP}, {"INT", SIGINT},
        {"KILL", SIGKILL}, {"QUIT", SIGQUIT}, {"ABRT", SIGABRT},
        {0, 0}
    };
    for (int i = 0; tab[i].n; i++)
        if (!strcasecmp(tab[i].n, s)) return tab[i].s;
    return atoi(s);
}

static pid_t child_pid = 0;
static int got_timeout = 0;

static void alarm_handler(int sig) {
    (void)sig;
    got_timeout = 1;
    if (child_pid > 0) kill(child_pid, SIGTERM);
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    int sig = SIGTERM;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-s") && argi + 1 < argc) { sig = sig_from_name(argv[++argi]); argi++; }
        else if (!strcmp(argv[argi], "--help")) { printf("Usage: timeout [-s SIGNAL] DURATION COMMAND [ARG]...\n"); return 0; }
        else break;
    }
    if (argi >= argc) { fprintf(stderr, "timeout: missing DURATION\n"); return 2; }
    struct timespec ts;
    if (parse_duration_ts(argv[argi++], &ts) < 0) { fprintf(stderr, "timeout: invalid duration\n"); return 2; }
    if (argi >= argc) { fprintf(stderr, "timeout: missing COMMAND\n"); return 2; }

    child_pid = fork();
    if (child_pid < 0) { perror("fork"); return 1; }
    if (child_pid == 0) {
        execvp(argv[argi], &argv[argi]);
        fprintf(stderr, "timeout: %s: %s\n", argv[argi], strerror(errno));
        _exit(127);
    }

    /* 设置闹钟 */
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    if (ts.tv_sec > 0 || ts.tv_nsec > 0) {
        if (nanosleep(&ts, NULL) == 0 || errno != EINTR) {
            /* 超时到了 */
            got_timeout = 1;
            kill(child_pid, sig);
        }
    }

    int status;
    waitpid(child_pid, &status, 0);
    if (got_timeout) {
        /* 发送了信号，返回 124 */
        return 124;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
