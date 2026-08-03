/* signame.c — 信号名 ↔ 编号转换
 *
 * 为 kill/timeout 等工具提供统一的信号名查询。
 * 消除 kill.c 和 timeout.c 中各自的信号表重复。
 *
 * 支持所有标准 POSIX 信号（SIGHUP..SIGTTOU + SIGBUS），
 * 以及通过数字字符串查询（"9" -> 9）。
 */
#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <stdlib.h>   /* atoi */
#include <stdio.h>    /* printf */

#include "meuos/utils.h"

static const struct { const char *name; int sig; } sigtab[] = {
    {"HUP",  SIGHUP},  {"INT",  SIGINT},  {"QUIT", SIGQUIT}, {"ILL",  SIGILL},
    {"ABRT", SIGABRT}, {"FPE",  SIGFPE},  {"KILL", SIGKILL}, {"SEGV", SIGSEGV},
    {"TERM", SIGTERM}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"PIPE", SIGPIPE},
    {"ALRM", SIGALRM}, {"CHLD", SIGCHLD}, {"CONT", SIGCONT}, {"STOP", SIGSTOP},
    {"TSTP", SIGTSTP}, {"TTIN", SIGTTIN}, {"TTOU", SIGTTOU}, {"BUS",  SIGBUS},
    {"TRAP", SIGTRAP}, {"URG",  SIGURG},  {"XCPU", SIGXCPU}, {"XFSZ", SIGXFSZ},
    {"VTALRM", SIGVTALRM}, {"PROF", SIGPROF}, {"WINCH", SIGWINCH},
    {"IO",   SIGIO},   {"PWR",  SIGPWR},  {"SYS",  SIGSYS},
    {NULL, 0}
};

int sig_from_name(const char *s) {
    if (!s || !*s) return -1;
    /* 跳过 "SIG" 前缀 */
    if (strncmp(s, "SIG", 3) == 0) s += 3;
    /* 先尝试名称匹配 */
    for (int i = 0; sigtab[i].name; i++)
        if (strcasecmp(sigtab[i].name, s) == 0)
            return sigtab[i].sig;
    /* 再尝试数字 */
    char *end;
    long val = strtol(s, &end, 10);
    if (*end == '\0' && val > 0 && val < 64)
        return (int)val;
    return -1;
}

const char *sig_to_name(int sig) {
    for (int i = 0; sigtab[i].name; i++)
        if (sigtab[i].sig == sig)
            return sigtab[i].name;
    return NULL;
}

void sig_list_all(void) {
    for (int i = 0; sigtab[i].name; i++) {
        printf("%2d) %-8s", sigtab[i].sig, sigtab[i].name);
        if ((i + 1) % 6 == 0 || !sigtab[i + 1].name)
            putchar('\n');
    }
}
