/* msh/exec/trap.c — trap 内建实现
 *
 * POSIX trap 子集：
 *   trap 'cmd' SIGNAL   — 捕获信号时执行 cmd
 *   trap '' SIGNAL      — 忽略信号
 *   trap - SIGNAL       — 恢复默认处理
 *   trap SIGNAL         — 恢复默认处理（同上）
 *   trap               — 列出所有已设置的 trap
 *
 * 支持的信号：INT TERM HUP QUIT EXIT（EXIT=0，shell 退出时触发）
 * 数字信号（1-31）也支持。
 *
 * 实现：静态表 + pending 标志（主循环轮询执行 trap 命令）。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msh/msh.h"
#include "msh/exec.h"

#define TRAP_MAX 33  /* 0=EXIT, 1-31=signals, 32=ERR(伪) */

static struct {
    char *cmd;    /* NULL=默认, ""=忽略(分配空串), 其他=命令字符串 */
    int active;   /* 是否已设置自定义处理 */
} g_traps[TRAP_MAX];

static volatile sig_atomic_t g_trap_pending = 0;
static int g_trap_save_status = 0;

/* 把信号名转为数字。支持 "INT" "SIGINT" "2" 等。返回 -1 表示未知。 */
int msh_signal_name_to_num(const char *name) {
    if (!name) return -1;
    /* 去掉 SIG 前缀 */
    const char *n = name;
    if (strncasecmp(n, "SIG", 3) == 0) n += 3;
    if (!strcasecmp(n, "HUP") || !strcasecmp(n, "1")) return 1;
    if (!strcasecmp(n, "INT") || !strcasecmp(n, "2")) return 2;
    if (!strcasecmp(n, "QUIT") || !strcasecmp(n, "3")) return 3;
    if (!strcasecmp(n, "KILL") || !strcasecmp(n, "9")) return 9;
    if (!strcasecmp(n, "TERM") || !strcasecmp(n, "15")) return 15;
    if (!strcasecmp(n, "USR1") || !strcasecmp(n, "10")) return 10;
    if (!strcasecmp(n, "USR2") || !strcasecmp(n, "12")) return 12;
    if (!strcasecmp(n, "EXIT")) return 0;
    if (!strcasecmp(n, "ERR")) return 32;
    /* 纯数字 */
    if (*name >= '0' && *name <= '9') {
        int v = atoi(name);
        if (v >= 0 && v < TRAP_MAX) return v;
    }
    return -1;
}

/* 数字转短名（用于 trap 列表显示） */
static const char *sig_short_name(int sig) {
    switch (sig) {
    case 0:  return "EXIT";
    case 1:  return "HUP";
    case 2:  return "INT";
    case 3:  return "QUIT";
    case 9:  return "KILL";
    case 10: return "USR1";
    case 12: return "USR2";
    case 15: return "TERM";
    case 32: return "ERR";
    default: {
        static char buf[16];
        snprintf(buf, sizeof(buf), "%d", sig);
        return buf;
    }
    }
}

static void msh_trap_handler(int sig) {
    (void)sig;
    g_trap_pending = sig + 1;  /* +1 避免 0 歧义 */
    /* 保存 $? 供 trap 命令内使用 */
    g_trap_save_status = msh_last_status;
}

/* 安装 sigaction handler（用于有自定义 trap 的信号） */
static void trap_install_handler(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = msh_trap_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, NULL);
}

/* trap 内建：trap [SPEC] [SIGNAL...]
 * SPEC: 命令字符串, "" (忽略), "-" (默认), 或 NULL (列出)
 * 返回 0 成功，2 用法错误。 */
int msh_builtin_trap(int argc, char **argv) {
    /* 找出所有信号名/数字 */
    const char *spec = NULL;
    int sigs[64];
    int nsigs = 0;

    /* 第一个参数如果不是信号名/数字，就是 spec（命令字符串） */
    int argi = 1;
    if (argc > 1) {
        /* 检查 argv[1] 是否是信号名或数字或 - */
        const char *first = argv[1];
        if (first[0] == '\0') {
            /* 空字符串 spec（忽略） */
            spec = "";
            argi = 2;
        } else if (first[0] == '-' && first[1] == '\0') {
            spec = "-";
            argi = 2;
        } else if (msh_signal_name_to_num(first) >= 0) {
            /* 是信号名，没有 spec => 列出或恢复默认 */
            spec = NULL;
            argi = 1;
        } else {
            /* 是命令字符串 */
            spec = first;
            argi = 2;
        }
    }

    /* 收集所有信号参数 */
    for (; argi < argc && nsigs < 64; argi++) {
        int sig = msh_signal_name_to_num(argv[argi]);
        if (sig < 0) {
            fprintf(stderr, "msh: trap: %s: unknown signal\n", argv[argi]);
            return 2;
        }
        sigs[nsigs++] = sig;
    }

    /* 无信号参数 => 列出所有 trap */
    if (nsigs == 0) {
        if (spec) {
            fprintf(stderr, "msh: trap: usage: trap [spec] [signal ...]\n");
            return 2;
        }
        for (int i = 0; i < TRAP_MAX; i++) {
            if (g_traps[i].active) {
                if (g_traps[i].cmd == NULL) {
                    printf("trap -- '-' %s\n", sig_short_name(i));
                } else if (g_traps[i].cmd[0] == '\0') {
                    printf("trap -- '' %s\n", sig_short_name(i));
                } else {
                    printf("trap -- '%s' %s\n", g_traps[i].cmd, sig_short_name(i));
                }
            }
        }
        return 0;
    }

    /* 设置 trap */
    for (int j = 0; j < nsigs; j++) {
        int sig = sigs[j];
        if (sig == 0 || sig == 32) {
            /* EXIT/ERR 无需安装 signal handler，仅记录 */
            free(g_traps[sig].cmd);
            if (spec && spec[0] == '-') {
                g_traps[sig].cmd = NULL;
                g_traps[sig].active = 0;
            } else if (spec && spec[0] == '\0') {
                g_traps[sig].cmd = strdup("");
                g_traps[sig].active = 1;
            } else if (spec) {
                g_traps[sig].cmd = strdup(spec);
                g_traps[sig].active = 1;
            }
            continue;
        }
        /* 真实信号 */
        free(g_traps[sig].cmd);
        if (spec && spec[0] == '-') {
            g_traps[sig].cmd = NULL;
            g_traps[sig].active = 0;
            signal(sig, SIG_DFL);
        } else if (spec && spec[0] == '\0') {
            g_traps[sig].cmd = strdup("");
            g_traps[sig].active = 1;
            signal(sig, SIG_IGN);
        } else if (spec) {
            g_traps[sig].cmd = strdup(spec);
            g_traps[sig].active = 1;
            trap_install_handler(sig);
        }
    }
    return 0;
}

/* 主循环轮询：检查并执行 pending trap。在每次读命令前调用。 */
void msh_trap_check(void) {
    if (!g_trap_pending) return;
    int pending = (int)g_trap_pending - 1;
    g_trap_pending = 0;
    if (pending < 0 || pending >= TRAP_MAX) return;
    if (!g_traps[pending].active) return;
    if (g_traps[pending].cmd && g_traps[pending].cmd[0] != '\0') {
        /* 执行 trap 命令 */
        int saved = msh_last_status;
        msh_last_status = g_trap_save_status;
        msh_run_string(g_traps[pending].cmd, strlen(g_traps[pending].cmd));
        msh_last_status = saved;
    }
}

/* EXIT trap：shell 退出时执行。 */
void msh_trap_exit(void) {
    if (g_traps[0].active && g_traps[0].cmd && g_traps[0].cmd[0] != '\0') {
        msh_last_status = 0;  /* EXIT trap 中 $? 是退出码，暂用 0 */
        msh_run_string(g_traps[0].cmd, strlen(g_traps[0].cmd));
    }
}
