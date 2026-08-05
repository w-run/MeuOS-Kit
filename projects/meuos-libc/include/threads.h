#ifndef MEUOS_THREADS_H
#define MEUOS_THREADS_H

#include <features.h>
#include <time.h>

typedef int (*thrd_start_t)(void *);
typedef void (*tss_dtor_t)(void *);

struct meuos_thread;
typedef struct meuos_thread *thrd_t;
typedef unsigned int tss_t;
typedef struct {
	_Atomic int state;
	pid_t owner;
	unsigned int count;
	int type;
} mtx_t;
typedef struct { _Atomic int sequence; } cnd_t;
typedef struct { _Atomic int state; } once_flag;

#define thrd_success 0
#define thrd_error 2
#define thrd_nomem 3
#define thrd_timedout 4
#define mtx_plain 0
#define mtx_recursive 1
#define mtx_timed 2
#define mtx_plain_init { 0, 0, 0, mtx_plain }
#define cnd_init_value { 0 }
#define ONCE_FLAG_INIT { 0 }
#define TSS_DTOR_ITERATIONS 4

int thrd_create(thrd_t *, thrd_start_t, void *);
int thrd_join(thrd_t, int *);
_Noreturn void thrd_exit(int);
int thrd_sleep(const struct timespec *, struct timespec *);
void thrd_yield(void);
int tss_create(tss_t *, tss_dtor_t);
void tss_delete(tss_t);
void *tss_get(tss_t);
int tss_set(tss_t, void *);
int mtx_init(mtx_t *, int);
int mtx_lock(mtx_t *);
int mtx_trylock(mtx_t *);
int mtx_timedlock(mtx_t *, const struct timespec *);
int mtx_unlock(mtx_t *);
void mtx_destroy(mtx_t *);
int cnd_init(cnd_t *);
int cnd_signal(cnd_t *);
int cnd_broadcast(cnd_t *);
int cnd_wait(cnd_t *, mtx_t *);
int cnd_timedwait(cnd_t *, mtx_t *, const struct timespec *);
void cnd_destroy(cnd_t *);
void call_once(once_flag *, void (*)(void));

#endif
