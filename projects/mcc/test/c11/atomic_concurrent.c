#include <stdatomic.h>

extern int printf(const char *, ...);
extern int puts(const char *);
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *, void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);

static atomic_int counter;

static void *
worker(void *arg)
{
	int i;
	(void)arg;
	for (i = 0; i < 1000; ++i)
		counter++;
	return 0;
}

int
main(void)
{
	pthread_t first, second;

	pthread_create(&first, 0, worker, 0);
	pthread_create(&second, 0, worker, 0);
	pthread_join(first, 0);
	pthread_join(second, 0);
	if (counter != 2000) {
		printf("FAIL: counter = %d\\n", counter);
		return 1;
	}
	puts("PASS");
	return 0;
}
