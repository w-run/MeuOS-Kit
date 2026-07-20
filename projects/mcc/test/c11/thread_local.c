#include <stdatomic.h>

extern int puts(const char *);
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);

static _Thread_local int local;
static atomic_int ready;

static void *
worker(void *argument)
{
	int *result = argument;
	int i;

	local = *result;
	atomic_fetch_add(&ready, 1);
	while (atomic_load(&ready) != 2)
		;
	for (i = 0; i < 1000; ++i)
		local += 0;
	*result = local;
	return 0;
}

int
main(void)
{
	pthread_t first_thread, second_thread;
	int first = 3, second = 7;

	pthread_create(&first_thread, 0, worker, &first);
	pthread_create(&second_thread, 0, worker, &second);
	pthread_join(first_thread, 0);
	pthread_join(second_thread, 0);
	if (first != 3 || second != 7 || local != 0) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
