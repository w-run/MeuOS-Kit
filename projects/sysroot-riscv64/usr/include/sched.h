#ifndef MEUOS_SCHED_H
#define MEUOS_SCHED_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sched_param {
	int sched_priority;
};

int sched_yield(void);
int sched_getparam(pid_t, struct sched_param *);
int sched_setscheduler(pid_t, int, const struct sched_param *);
int sched_getscheduler(pid_t);
int sched_setparam(pid_t, const struct sched_param *);
int sched_getparam(pid_t, struct sched_param *);
int sched_get_priority_max(int);
int sched_get_priority_min(int);

#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2

#ifdef __cplusplus
}
#endif

#endif
