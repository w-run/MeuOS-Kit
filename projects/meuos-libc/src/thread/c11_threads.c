/* thread/c11_threads.c -- thrd_create / thrd_join / thrd_exit / thrd_*
 *
 * Implements the C11 thread primitives on top of clone(2) and futex(2).
 * Per-thread state lives in a struct meuos_thread (see internal.h)
 * stored in a global slot table (see state.c); thrd_join spins on a
 * futex keyed on the thread's tid slot to wait for the clone child to
 * publish its tid (and on exit, to publish its result and wake waiters). */

#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>
#include "../internal/syscall.h"
#include "internal.h"

_Noreturn void
__meuos_thread_finish(int result, struct meuos_thread *control)
{
	__meuos_tss_cleanup(gettid());
	if (control) {
		control->result = result;
		/* Manually clear the tid and wake any thrd_join waiter, rather
		 * than relying on the kernel's CLONE_CHILD_CLEARTID path: on
		 * i386 the clone child_tidptr argument has proven unreliable
		 * to observe, so publish the exit explicitly. */
		control->tid = 0;
		__syscall6(LINUX_SYS_FUTEX, (long)&control->tid, 1, 1, 0, 0, 0);
	}
	_exit(0);
}

int
thrd_create(thrd_t *thread, thrd_start_t start, void *argument)
{
	struct meuos_thread *control;
	long result;

	if (!thread || !start)
		return thrd_error;
	control = malloc(sizeof(*control));
	if (!control)
		return thrd_nomem;
	control->tid = 0;
	control->result = 0;
	control->tls = __meuos_tls_alloc();
	control->tls_size = __meuos_tls_size();
	if (control->tls_size && !control->tls) {
		free(control);
		return thrd_nomem;
	}
	control->stack = mmap(0, THREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (control->stack == MAP_FAILED) {
		if (control->tls)
			__meuos_tls_free(control->tls);
		free(control);
		return thrd_nomem;
	}
	if (!__meuos_control_add(control)) {
		if (control->tls)
			__meuos_tls_free(control->tls);
		munmap(control->stack, THREAD_STACK_SIZE);
		free(control);
		return thrd_nomem;
	}
	result = __meuos_thread_clone(start, argument,
		(char *)control->stack + THREAD_STACK_SIZE, control, control->tls);
	if (__syscall_error(result)) {
		__meuos_control_remove(control);
		if (control->tls)
			__meuos_tls_free(control->tls);
		munmap(control->stack, THREAD_STACK_SIZE);
		free(control);
		return thrd_error;
	}
	*thread = control;
	return thrd_success;
}

int
thrd_join(thrd_t thread, int *result)
{
	if (!thread)
		return thrd_error;
	while (thread->tid) {
		int observed = thread->tid;
		if (observed)
			__syscall6(LINUX_SYS_FUTEX, (long)&thread->tid, FUTEX_WAIT, observed, 0, 0, 0);
	}
	if (result)
		*result = thread->result;
	__meuos_control_remove(thread);
	if (thread->tls)
		__meuos_tls_free(thread->tls);
	munmap(thread->stack, THREAD_STACK_SIZE);
	free(thread);
	return thrd_success;
}

_Noreturn void
thrd_exit(int result)
{
	__meuos_thread_finish(result, __meuos_control_current(gettid()));
}

int
thrd_sleep(const struct timespec *duration, struct timespec *remaining)
{
	return nanosleep(duration, remaining) == 0 ? thrd_success : thrd_error;
}

void
thrd_yield(void)
{
	sched_yield();
}
