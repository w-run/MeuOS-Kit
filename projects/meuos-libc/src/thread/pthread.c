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

/* ---- rwlock ---- */
int
pthread_rwlock_init(pthread_rwlock_t *rw, const pthread_rwlockattr_t *attr)
{
	(void)attr;
	rw->readers = 0; rw->writer = 0;
	mtx_init(&rw->lock, mtx_plain);
	cnd_init(&rw->rcond);
	cnd_init(&rw->wcond);
	return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
	(void)rw; return 0;
}
int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
	mtx_lock(&rw->lock);
	while (rw->writer) cnd_wait(&rw->rcond, &rw->lock);
	rw->readers++;
	mtx_unlock(&rw->lock);
	return 0;
}
int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
	mtx_lock(&rw->lock);
	while (rw->readers || rw->writer) cnd_wait(&rw->wcond, &rw->lock);
	rw->writer = 1;
	mtx_unlock(&rw->lock);
	return 0;
}
int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
	mtx_lock(&rw->lock);
	if (rw->writer) { rw->writer = 0; cnd_signal(&rw->wcond); }
	else { rw->readers--; if (rw->readers == 0) cnd_signal(&rw->wcond); }
	cnd_broadcast(&rw->rcond);
	mtx_unlock(&rw->lock);
	return 0;
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw) {
	mtx_lock(&rw->lock);
	if (rw->writer) { mtx_unlock(&rw->lock); return EBUSY; }
	rw->readers++;
	mtx_unlock(&rw->lock);
	return 0;
}
int pthread_rwlock_trywrlock(pthread_rwlock_t *rw) {
	mtx_lock(&rw->lock);
	if (rw->readers || rw->writer) { mtx_unlock(&rw->lock); return EBUSY; }
	rw->writer = 1;
	mtx_unlock(&rw->lock);
	return 0;
}

/* ---- barrier ---- */
int
pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *attr, unsigned count)
{
	(void)attr;
	b->count = (int)count;
	b->reached = 0;
	mtx_init(&b->lock, mtx_plain);
	cnd_init(&b->cond);
	return 0;
}
int pthread_barrier_destroy(pthread_barrier_t *b) { (void)b; return 0; }
int
pthread_barrier_wait(pthread_barrier_t *b)
{
	mtx_lock(&b->lock);
	b->reached++;
	if (b->reached >= b->count) {
		b->reached = 0;
		cnd_broadcast(&b->cond);
		mtx_unlock(&b->lock);
		return 1; /* PTHREAD_BARRIER_SERIAL_THREAD */
	}
	cnd_wait(&b->cond, &b->lock);
	mtx_unlock(&b->lock);
	return 0;
}

/* ---- spinlock ---- */
int pthread_spin_init(pthread_spinlock_t *sp, int pshared) { (void)pshared; *((int *)sp) = 0; return 0; }
int pthread_spin_destroy(pthread_spinlock_t *sp) { (void)sp; return 0; }

#if defined(__x86_64__) || defined(__i386__)
int
pthread_spin_lock(pthread_spinlock_t *sp)
{
	while (__sync_lock_test_and_set(sp, 1))
		while (*sp) asm volatile("pause" ::: "memory");
	return 0;
}
int
pthread_spin_trylock(pthread_spinlock_t *sp)
{
	return __sync_lock_test_and_set(sp, 1) ? EBUSY : 0;
}
int
pthread_spin_unlock(pthread_spinlock_t *sp)
{
	__sync_lock_release(sp);
	return 0;
}
#else
/* Generic implementation: non-atomic for targets where
 * __sync builtins aren't available. */
int
pthread_spin_lock(pthread_spinlock_t *sp)
{
	while (*((int *)sp)) ;
	*((int *)sp) = 1;
	return 0;
}
int
pthread_spin_trylock(pthread_spinlock_t *sp)
{
	if (*((int *)sp)) return EBUSY;
	*((int *)sp) = 1;
	return 0;
}
int
pthread_spin_unlock(pthread_spinlock_t *sp)
{
	*((int *)sp) = 0;
	return 0;
}
#endif

/* ---- cleanup handlers ---- */
static struct pthread_cleanup_buffer *cleanup_stack;

void
_pthread_cleanup_push(struct pthread_cleanup_buffer *buf,
                      void (*routine)(void *), void *arg)
{
	buf->routine = routine;
	buf->arg = arg;
	buf->prev = cleanup_stack;
	cleanup_stack = buf;
}

void
_pthread_cleanup_pop(struct pthread_cleanup_buffer *buf, int execute)
{
	cleanup_stack = buf->prev;
	if (execute) buf->routine(buf->arg);
}
