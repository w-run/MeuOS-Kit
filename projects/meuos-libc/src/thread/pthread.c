#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <threads.h>

/* Minimal POSIX threads built on the existing C11 thread primitives
 * (clone/futex/mutex/condvar).  pthread_create adapts the pthread-style
 * start routine (returns void *) to the C11 start routine (returns int). */

static tss_t self_key;
static once_flag self_key_once = ONCE_FLAG_INIT;

static void
self_key_init(void)
{
	tss_create(&self_key, (tss_dtor_t)0);
}

/* Trampoline runs in the new thread: publish self so pthread_self works,
 * then invoke the user routine and stash its result for pthread_join. */
static int
pthread_trampoline(void *arg)
{
	struct pthread *self = arg;

	tss_set(self_key, self);
	self->result = self->start(self->arg);
	return 0;
}

int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start)(void *), void *arg)
{
	struct pthread *self;
	(void)attr;

	call_once(&self_key_once, self_key_init);
	self = malloc(sizeof(*self));
	if (!self)
		return EAGAIN;
	self->start = start;
	self->arg = arg;
	self->result = (void *)0;
	if (thrd_create(&self->thread, pthread_trampoline, self) != thrd_success) {
		free(self);
		return EAGAIN;
	}
	*thread = self;
	return 0;
}

int
pthread_join(pthread_t thread, void **retval)
{
	int status;

	if (thrd_join(thread->thread, &status) != thrd_success)
		return EINVAL;
	if (retval)
		*retval = thread->result;
	free(thread);
	return 0;
}

_Noreturn void
pthread_exit(void *retval)
{
	pthread_t self = pthread_self();

	if (self)
		self->result = retval;
	thrd_exit(0);
}

pthread_t
pthread_self(void)
{
	call_once(&self_key_once, self_key_init);
	return (pthread_t)tss_get(self_key);
}

int
pthread_equal(pthread_t a, pthread_t b)
{
	return a == b;
}

int
pthread_attr_init(pthread_attr_t *attr)
{
	attr->stack = (void *)0;
	attr->stacksize = 0;
	attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

int
pthread_attr_destroy(pthread_attr_t *attr)
{
	(void)attr;
	return 0;
}

int
pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
	if (detachstate != PTHREAD_CREATE_JOINABLE &&
	    detachstate != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	attr->detachstate = detachstate;
	return 0;
}

int
pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
	*detachstate = attr->detachstate;
	return 0;
}

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	int type = attr ? attr->type : mtx_plain;

	return mtx_init(mutex, type) == thrd_success ? 0 : -1;
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	mtx_destroy(mutex);
	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
	return mtx_lock(mutex) == thrd_success ? 0 : -1;
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	return mtx_unlock(mutex) == thrd_success ? 0 : -1;
}

int
pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return mtx_trylock(mutex) == thrd_success ? 0 : -1;
}

int
pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	(void)attr;
	return cnd_init(cond) == thrd_success ? 0 : -1;
}

int
pthread_cond_destroy(pthread_cond_t *cond)
{
	cnd_destroy(cond);
	return 0;
}

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	return cnd_wait(cond, mutex) == thrd_success ? 0 : -1;
}

int
pthread_cond_signal(pthread_cond_t *cond)
{
	return cnd_signal(cond) == thrd_success ? 0 : -1;
}

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
	return cnd_broadcast(cond) == thrd_success ? 0 : -1;
}

int
pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
	return tss_create(key, destructor) == thrd_success ? 0 : EAGAIN;
}

int
pthread_key_delete(pthread_key_t key)
{
	tss_delete(key);
	return 0;
}

void *
pthread_getspecific(pthread_key_t key)
{
	return tss_get(key);
}

int
pthread_setspecific(pthread_key_t key, const void *value)
{
	return tss_set(key, (void *)value) == thrd_success ? 0 : -1;
}
