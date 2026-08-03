/* msh/job.h — 作业控制公共 API
 *
 * 后台作业表 + jobs/fg/bg 支持。
 * 今日范围：后台 `&` 启动、jobs 列出、fg/bg 切换。
 * 不做：job 退出异步通知（SIGCHLD 仅标记，下次 prompt 前回收）、
 *      pipeline 中的 &、job 表自动清理。
 */
#ifndef MEUOS_MSH_JOB_H
#define MEUOS_MSH_JOB_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSH_MAX_JOBS 64

/* 作业状态 */
enum {
    MSH_JOB_RUNNING,
    MSH_JOB_STOPPED,
    MSH_JOB_DONE,
};

typedef struct {
    pid_t pid;
    int state;              /* MSH_JOB_* */
    char *cmdline;          /* 可读命令行（malloc） */
    int job_id;             /* 1-based 作业编号 */
} msh_job_t;

/* 注册一个后台作业。返回 job id（1-based）或 -1。 */
int msh_job_add(pid_t pid, const char *cmdline);

/* 打印所有作业（builtin jobs 用）。 */
void msh_job_list(void);

/* fg JOBID：把作业带回前台并等待。返回退出码。 */
int msh_job_fg(int job_id);

/* bg JOBID：继续已停止的作业。返回 0 或 -1。 */
int msh_job_bg(int job_id);

/* 回收已结束的作业（在 prompt 前调用）。 */
void msh_job_reap(void);

/* 按 job_id 查作业。 */
msh_job_t *msh_job_get(int job_id);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_JOB_H */
