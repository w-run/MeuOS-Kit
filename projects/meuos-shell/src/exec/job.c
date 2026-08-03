/* msh/exec/job.c — 作业控制实现
 *
 * 定长作业表 + SIGCHLD 标记回收。
 * 简化设计：不做完整进程组管理，后台作业直接 fork 执行。
 * jobs/fg/bg 为 builtin，见 parse.c 中的 msh_run_builtin。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msh/job.h"

static msh_job_t g_jobs[MSH_MAX_JOBS];
static int g_job_count = 0;
static int g_next_job_id = 1;

/* SIGCHLD handler：仅置标志（异步信号安全） */
static volatile sig_atomic_t g_sigchld = 0;

void msh_job_sigchld_handler(int sig) {
    (void)sig;
    g_sigchld = 1;
}

int msh_job_add(pid_t pid, const char *cmdline) {
    if (g_job_count >= MSH_MAX_JOBS) return -1;
    msh_job_t *j = &g_jobs[g_job_count];
    j->pid = pid;
    j->state = MSH_JOB_RUNNING;
    j->cmdline = strdup(cmdline ? cmdline : "");
    j->job_id = g_next_job_id++;
    g_job_count++;
    return j->job_id;
}

msh_job_t *msh_job_get(int job_id) {
    for (int i = 0; i < g_job_count; i++) {
        if (g_jobs[i].job_id == job_id) return &g_jobs[i];
    }
    return NULL;
}

void msh_job_list(void) {
    for (int i = 0; i < g_job_count; i++) {
        msh_job_t *j = &g_jobs[i];
        const char *state_str;
        switch (j->state) {
        case MSH_JOB_RUNNING: state_str = "Running"; break;
        case MSH_JOB_STOPPED: state_str = "Stopped"; break;
        default:              state_str = "Done";    break;
        }
        printf("[%d] %-8s %d  %s\n", j->job_id, state_str, (int)j->pid,
               j->cmdline ? j->cmdline : "");
    }
}

void msh_job_reap(void) {
    if (!g_sigchld) return;
    g_sigchld = 0;
    for (int i = 0; i < g_job_count; i++) {
        msh_job_t *j = &g_jobs[i];
        if (j->state == MSH_JOB_RUNNING) {
            int status;
            pid_t r = waitpid(j->pid, &status, WNOHANG);
            if (r == j->pid) {
                if (WIFSTOPPED(status)) {
                    j->state = MSH_JOB_STOPPED;
                } else {
                    j->state = MSH_JOB_DONE;
                }
            }
        }
    }
}

int msh_job_fg(int job_id) {
    msh_job_t *j = msh_job_get(job_id);
    if (!j) {
        fprintf(stderr, "msh: fg: no such job: %d\n", job_id);
        return 1;
    }
    if (j->state == MSH_JOB_STOPPED) {
        kill(j->pid, SIGCONT);
        j->state = MSH_JOB_RUNNING;
    }
    /* 等待前台 */
    int status;
    if (waitpid(j->pid, &status, 0) < 0) return 1;
    if (WIFEXITED(status)) {
        j->state = MSH_JOB_DONE;
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        j->state = MSH_JOB_DONE;
        return 128 + WTERMSIG(status);
    }
    j->state = MSH_JOB_DONE;
    return 0;
}

int msh_job_bg(int job_id) {
    msh_job_t *j = msh_job_get(job_id);
    if (!j) {
        fprintf(stderr, "msh: bg: no such job: %d\n", job_id);
        return 1;
    }
    if (j->state == MSH_JOB_STOPPED) {
        if (kill(j->pid, SIGCONT) < 0) return 1;
        j->state = MSH_JOB_RUNNING;
        printf("[%d] %d  %s\n", j->job_id, (int)j->pid,
               j->cmdline ? j->cmdline : "");
    }
    return 0;
}
