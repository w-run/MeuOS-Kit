#include <errno.h>
#include <stdio.h>
#include <threads.h>

static _Thread_local int local_value = 5;
static int worker_value;
static int worker_errno;

static int
worker(void *argument)
{
	(void)argument;
	local_value = 9;
	errno = 47;
	worker_value = local_value;
	worker_errno = errno;
	return 0;
}

int
main(void)
{
	thrd_t thread;

	errno = 31;
	if (thrd_create(&thread, worker, 0) != thrd_success
	 || thrd_join(thread, 0) != thrd_success)
		return 1;
	printf("tls main=%d child=%d errno=%d/%d\n", local_value, worker_value,
		errno, worker_errno);
	return local_value != 5 || worker_value != 9 || errno != 31 || worker_errno != 47;
}
