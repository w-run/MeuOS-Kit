/* thread/mutex.c -- C11 mutex (mtx_*) primitives.
 *
 * Implements recursive and timed mutexes on top of an atomic state
 * variable and the futex(2) syscall. The owner field enables
 * mtx_recursive; the count tracks reentrant depth. */

#include <errno.h>
#include <stdatomic.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include "../internal/syscall.h"
#include "internal.h"

int
mtx_init(mtx_t *mutex, int type)
{
	if (!mutex || (type & ~(mtx_recursive | mtx_timed)))
		return thrd_error;
	atomic_store(&mutex->state, 0);
	mutex->owner = 0;
	mutex->count = 0;
	mutex->type = type;
	return thrd_success;
}

int
mtx_trylock(mtx_t *mutex)
{
	int expected = 0;
	pid_t tid;

	if (!mutex)
		return thrd_error;
	tid = gettid();
	if ((mutex->type & mtx_recursive) && mutex->owner == tid) {
		++mutex->count;
		return thrd_success;
	}
	if (!atomic_compare_exchange_strong(&mutex->state, &expected, 1))
		return thrd_error;
	mutex->owner = tid;
	mutex->count = 1;
	return thrd_success;
}

int
mtx_lock(mtx_t *mutex)
{
	if (!mutex)
		return thrd_error;
	while (mtx_trylock(mutex) != thrd_success)
		__syscall6(LINUX_SYS_FUTEX, (long)&mutex->state, FUTEX_WAIT, 1, 0, 0, 0);
	return thrd_success;
}

int
mtx_timedlock(mtx_t *mutex, const struct timespec *deadline)
{
	if (!mutex || !deadline)
		return thrd_error;
	for (;;) {
		struct timespec now;
		struct timespec remaining;
		long value;
		if (mtx_trylock(mutex) == thrd_success)
			return thrd_success;
		if (clock_gettime(CLOCK_REALTIME, &now) != 0)
			return thrd_error;
		remaining.tv_sec = deadline->tv_sec - now.tv_sec;
		remaining.tv_nsec = deadline->tv_nsec - now.tv_nsec;
		if (remaining.tv_nsec < 0) {
			--remaining.tv_sec;
			remaining.tv_nsec += 1000000000;
		}
		if (remaining.tv_sec < 0)
			return thrd_timedout;
#if defined(__i386__)
		/* i386 futex (240) takes a 32-bit time_t timeout, which
		 * mismatches our 64-bit time_t.  Use futex_time64 (422)
		 * with a 64-bit timespec, mirroring cnd_timedwait. */
		{
			struct { int64_t tv_sec, tv_nsec; } timeout64;
			timeout64.tv_sec = remaining.tv_sec;
			timeout64.tv_nsec = remaining.tv_nsec;
			value = __syscall6(422, (long)&mutex->state, FUTEX_WAIT,
				1, (long)&timeout64, 0, 0);
		}
#else
		value = __syscall6(LINUX_SYS_FUTEX, (long)&mutex->state, FUTEX_WAIT,
			1, (long)&remaining, 0, 0);
#endif
		if (__syscall_error(value) && -value == ETIMEDOUT)
			return thrd_timedout;
		if (__syscall_error(value) && -value != EINTR && -value != EAGAIN)
			return thrd_error;
	}
}

int
mtx_unlock(mtx_t *mutex)
{
	if (!mutex)
		return thrd_error;
	if (mutex->owner != gettid() || !mutex->count)
		return thrd_error;
	if ((mutex->type & mtx_recursive) && --mutex->count)
		return thrd_success;
	mutex->owner = 0;
	mutex->count = 0;
	atomic_store(&mutex->state, 0);
	__syscall6(LINUX_SYS_FUTEX, (long)&mutex->state, 1, 1, 0, 0, 0);
	return thrd_success;
}

void
mtx_destroy(mtx_t *mutex)
{
	if (!mutex)
		return;
	mutex->owner = 0;
	mutex->count = 0;
	atomic_store(&mutex->state, 0);
}
