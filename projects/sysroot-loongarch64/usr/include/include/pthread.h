#ifndef MEUOS_PTHREAD_H
#define MEUOS_PTHREAD_H

#include <stddef.h>
#include <threads.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal POSIX threads subset.  The runtime is built on the existing
 * C11 thread primitives (clone/futex/mutex/condvar); pthread_create adapts
 * the pthread-style start routine to the C11 one. */

struct pthread {
	thrd_t thread;
	void *(*start)(void *);
	void *arg;
	void *result;
};
typedef struct pthread *pthread_t;

typedef struct {
	void *stack;
	size_t stacksize;
	int detachstate;
} pthread_attr_t;
typedef struct { int type; } pthread_mutexattr_t;
typedef struct { int dummy; } pthread_condattr_t;
typedef mtx_t pthread_mutex_t;
typedef cnd_t pthread_cond_t;
typedef tss_t pthread_key_t;

#define PTHREAD_MUTEX_INITIALIZER   mtx_plain_init
#define PTHREAD_COND_INITIALIZER     cnd_init_value
#define PTHREAD_CREATE_JOINABLE     0
#define PTHREAD_CREATE_DETACHED     1
#define PTHREAD_MUTEX_NORMAL       mtx_plain
#define PTHREAD_MUTEX_RECURSIVE    mtx_recursive

int pthread_create(pthread_t *, const pthread_attr_t *,
    void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
_Noreturn void pthread_exit(void *);
pthread_t pthread_self(void);
int pthread_equal(pthread_t, pthread_t);

int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
int pthread_attr_getdetachstate(const pthread_attr_t *, int *);

int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutex_trylock(pthread_mutex_t *);

int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);

int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_key_delete(pthread_key_t);
void *pthread_getspecific(pthread_key_t);
int pthread_setspecific(pthread_key_t, const void *);

#ifdef __cplusplus
}
#endif

#endif
