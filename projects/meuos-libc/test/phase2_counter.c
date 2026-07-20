#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

static _Atomic int counter;

static int
worker(void *argument)
{
	int index;
	(void)argument;
	for (index = 0; index < 1000; ++index)
		counter++;
	return 0;
}

int
main(void)
{
	thrd_t first;
	thrd_t second;

	if (thrd_create(&first, worker, 0) != thrd_success
	 || thrd_create(&second, worker, 0) != thrd_success)
		return 1;
	if (thrd_join(first, 0) != thrd_success || thrd_join(second, 0) != thrd_success)
		return 1;
	printf("counter = %d\n", counter);
	return counter != 2000;
}
