#include <pthread.h>
#include <stdio.h>

static int shared;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready;

static void *
worker(void *arg)
{
	long n = (long)arg;
	pthread_mutex_lock(&mtx);
	shared += (int)n;
	ready = 1;
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mtx);
	return (void *)(long)(n * 2);
}

int
main(void)
{
	pthread_t t;
	void *ret;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	if (pthread_create(&t, &attr, worker, (void *)21) != 0) {
		printf("pthread_create failed\n");
		return 1;
	}
	pthread_attr_destroy(&attr);

	pthread_mutex_lock(&mtx);
	while (!ready)
		pthread_cond_wait(&cv, &mtx);
	pthread_mutex_unlock(&mtx);

	if (pthread_join(t, &ret) != 0) {
		printf("pthread_join failed\n");
		return 2;
	}
	if (shared != 21) {
		printf("shared=%d\n", shared);
		return 3;
	}
	if ((long)ret != 42) {
		printf("ret=%ld\n", (long)ret);
		return 4;
	}

	puts("pthread ok");
	return 0;
}
