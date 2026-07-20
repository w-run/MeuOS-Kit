#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

static _Atomic int counter;
static mtx_t mutex = mtx_plain_init;
static mtx_t recursive_mutex = mtx_plain_init;
static cnd_t condition = cnd_init_value;
static once_flag once = ONCE_FLAG_INIT;
static int protected_counter;
static int ready;
static int once_counter;
static tss_t thread_key;
static _Atomic int tss_destructed;

static void
initialize_once(void)
{
	++once_counter;
}

static void
destroy_thread_value(void *value)
{
	if ((long)value == 7)
		++tss_destructed;
}

static int
worker(void *argument)
{
	int index;
	(void)argument;
	if (tss_set(thread_key, (void *)(long)7) != thrd_success
	 || (long)tss_get(thread_key) != 7)
		return 1;
	call_once(&once, initialize_once);
	for (index = 0; index < 1000; ++index)
		counter++;
	for (index = 0; index < 1000; ++index) {
		mtx_lock(&mutex);
		++protected_counter;
		mtx_unlock(&mutex);
	}
	mtx_lock(&mutex);
	ready = 1;
	cnd_signal(&condition);
	mtx_unlock(&mutex);
	return 9;
}

static int
exiting_worker(void *argument)
{
	(void)argument;
	if (tss_set(thread_key, (void *)(long)7) != thrd_success)
		return 1;
	thrd_exit(13);
}

int
main(void)
{
	thrd_t first;
	thrd_t second;
	thrd_t third;
	int result;
	struct timespec pause;
	struct timespec deadline;

	pause.tv_sec = 0;
	pause.tv_nsec = 1;
	if (thrd_sleep(&pause, 0) != thrd_success)
		return 1;
	thrd_yield();
	if (tss_create(&thread_key, destroy_thread_value) != thrd_success)
		return 1;
	if (tss_set(thread_key, (void *)(long)3) != thrd_success
	 || (long)tss_get(thread_key) != 3)
		return 1;
	if (mtx_init(&recursive_mutex, mtx_recursive | mtx_timed) != thrd_success
	 || mtx_lock(&recursive_mutex) != thrd_success
	 || mtx_trylock(&recursive_mutex) != thrd_success
	 || mtx_unlock(&recursive_mutex) != thrd_success
	 || mtx_unlock(&recursive_mutex) != thrd_success)
		return 1;
	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		return 1;
	deadline.tv_nsec += 1000000;
	if (deadline.tv_nsec >= 1000000000) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000;
	}
	mtx_lock(&mutex);
	if (cnd_timedwait(&condition, &mutex, &deadline) != thrd_timedout) {
		mtx_unlock(&mutex);
		return 1;
	}
	mtx_unlock(&mutex);
	mtx_lock(&mutex);
	if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		return 1;
	deadline.tv_nsec += 1000000;
	if (deadline.tv_nsec >= 1000000000) {
		++deadline.tv_sec;
		deadline.tv_nsec -= 1000000000;
	}
	if (mtx_timedlock(&mutex, &deadline) != thrd_timedout) {
		mtx_unlock(&mutex);
		return 1;
	}
	mtx_unlock(&mutex);
	if (thrd_create(&first, worker, 0) != thrd_success || thrd_create(&second, worker, 0) != thrd_success)
		return 1;
	mtx_lock(&mutex);
	while (!ready)
		cnd_wait(&condition, &mutex);
	mtx_unlock(&mutex);
	if (thrd_join(first, &result) != thrd_success || result != 9 || thrd_join(second, 0) != thrd_success)
		return 1;
	if (thrd_create(&third, exiting_worker, 0) != thrd_success
	 || thrd_join(third, &result) != thrd_success || result != 13)
		return 1;
	if (counter != 2000 || protected_counter != 2000 || once_counter != 1)
		return 1;
	if (tss_destructed != 3 || (long)tss_get(thread_key) != 3)
		return 1;
	tss_delete(thread_key);
	if (tss_get(thread_key) != 0)
		return 1;
	puts("PASS C11 threads");
	return 0;
}
