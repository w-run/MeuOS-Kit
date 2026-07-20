#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

static atomic_int counter;

static int
worker(void *argument)
{
	int i;
	(void)argument;
	for (i = 0; i < 1000; ++i)
		counter++;
	return 0;
}

int
main(void)
{
	atomic_int value = 0;
	atomic_short small = -2;
	atomic_long large;
	atomic_flag flag = ATOMIC_FLAG_INIT;
	thrd_t first, second;
	int expected;

	atomic_store(&value, 3);
	atomic_thread_fence(memory_order_seq_cst);
	atomic_signal_fence(memory_order_acquire);
	if (!atomic_is_lock_free(&value)) {
		puts("FAIL");
		return 1;
	}
	if (atomic_load(&value) != 3 || atomic_fetch_or(&value, 4) != 3 ||
	    atomic_exchange(&value, 9) != 7) {
		puts("FAIL");
		return 1;
	}
	expected = 9;
	if (!atomic_compare_exchange_strong(&value, &expected, 11) || value != 11 ||
	    atomic_flag_test_and_set(&flag) || !atomic_flag_test_and_set(&flag)) {
		puts("FAIL");
		return 1;
	}
	atomic_store(&large, -5);
	if (atomic_fetch_add(&small, 3) != -2 || atomic_load(&small) != 1 ||
	    atomic_fetch_sub(&large, 4) != -5 || atomic_load(&large) != -9) {
		puts("FAIL");
		return 1;
	}

	if (thrd_create(&first, worker, 0) != thrd_success
	 || thrd_create(&second, worker, 0) != thrd_success
	 || thrd_join(first, 0) != thrd_success
	 || thrd_join(second, 0) != thrd_success) {
		puts("FAIL");
		return 1;
	}
	if (counter != 2000) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
