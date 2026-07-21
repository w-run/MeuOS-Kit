/* thread/condvar.c -- C11 condition variables (cnd_*) primitives.
 *
 * Implements cnd_t as an atomic sequence counter; cnd_signal/broadcast
 * bump the sequence and wake waiters; cnd_wait atomically unlocks the
 * mutex, blocks on a futex, and re-locks the mutex. The sequence trick
 * avoids lost wakeups even when the unlock happens before wait. */

#include <errno.h>
#include <stdatomic.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include "../internal/syscall.h"
#include "internal.h"

int
cnd_init(cnd_t *condition)
{
	if (!condition)
		return thrd_error;
	atomic_store(&condition->sequence, 0);
	return thrd_success;
}

int
cnd_signal(cnd_t *condition)
{
	if (!condition)
		return thrd_error;
	atomic_fetch_add(&condition->sequence, 1);
	__syscall6(LINUX_SYS_FUTEX, (long)&condition->sequence, 1, 1, 0, 0, 0);
	return thrd_success;
}

int
cnd_broadcast(cnd_t *condition)
{
	if (!condition)
		return thrd_error;
	atomic_fetch_add(&condition->sequence, 1);
	__syscall6(LINUX_SYS_FUTEX, (long)&condition->sequence, 1, 0x7fffffff, 0, 0, 0);
	return thrd_success;
}

int
cnd_wait(cnd_t *condition, mtx_t *mutex)
{
	int expected;

	if (!condition || !mutex)
		return thrd_error;
	expected = atomic_load(&condition->sequence);
	if (mtx_unlock(mutex) != thrd_success)
		return thrd_error;
	__syscall6(LINUX_SYS_FUTEX, (long)&condition->sequence, FUTEX_WAIT, expected, 0, 0, 0);
	return mtx_lock(mutex);
}

int
cnd_timedwait(cnd_t *condition, mtx_t *mutex, const struct timespec *deadline)
{
	struct timespec now;
	struct timespec remaining;
	int expected;
	long value;

	if (!condition || !mutex || !deadline)
		return thrd_error;
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
	expected = atomic_load(&condition->sequence);
	if (mtx_unlock(mutex) != thrd_success)
		return thrd_error;
#if defined(__i386__)
	{
		struct { int64_t tv_sec, tv_nsec; } timeout64;
		timeout64.tv_sec = remaining.tv_sec;
		timeout64.tv_nsec = remaining.tv_nsec;
		value = __syscall6(422, (long)&condition->sequence, FUTEX_WAIT,
			expected, (long)&timeout64, 0, 0);
	}
#else
	value = __syscall6(LINUX_SYS_FUTEX, (long)&condition->sequence, FUTEX_WAIT,
		expected, (long)&remaining, 0, 0);
#endif
	if (mtx_lock(mutex) != thrd_success)
		return thrd_error;
	if (__syscall_error(value) && -value == ETIMEDOUT)
		return thrd_timedout;
	return __syscall_error(value) && -value != EINTR && -value != EAGAIN ? thrd_error : thrd_success;
}

void
cnd_destroy(cnd_t *condition)
{
	(void)condition;
}
