#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

enum { worker_count = 4, iterations = 2000 };

static _Atomic int failed;

static int
worker(void *argument)
{
	int worker = (int)(long)argument;
	int iteration;

	for (iteration = 0; iteration < iterations; ++iteration) {
		size_t size = (size_t)((iteration + worker * 17) % 193 + 1);
		unsigned char *memory = malloc(size);
		unsigned char *grown;
		size_t index;

		if (!memory) {
			failed = 1;
			return 1;
		}
		for (index = 0; index < size; ++index)
			memory[index] = (unsigned char)(index + worker);
		grown = realloc(memory, size + 67);
		if (!grown) {
			failed = 1;
			free(memory);
			return 1;
		}
		for (index = 0; index < size; ++index)
			if (grown[index] != (unsigned char)(index + worker)) {
				failed = 1;
				free(grown);
				return 1;
			}
		free(grown);
	}
	return 0;
}

int
main(void)
{
	thrd_t workers[worker_count];
	int index;
	int result;

	for (index = 0; index < worker_count; ++index)
		if (thrd_create(&workers[index], worker, (void *)(long)index) != thrd_success)
			return 1;
	for (index = 0; index < worker_count; ++index)
		if (thrd_join(workers[index], &result) != thrd_success || result != 0)
			return 1;
	if (failed)
		return 1;
	puts("PASS concurrent malloc");
	return 0;
}
