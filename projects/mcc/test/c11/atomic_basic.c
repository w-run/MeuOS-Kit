#include <stdatomic.h>

extern int puts(const char *);

int
main(void)
{
	atomic_int value = 0;

	atomic_store(&value, 7);
	if (atomic_load(&value) != 7) {
		puts("FAIL");
		return 1;
	}
	atomic_store(&value, 0);
	if (atomic_fetch_add(&value, 1) != 0 || value != 1) {
		puts("FAIL");
		return 1;
	}
	if (atomic_fetch_sub_explicit(&value, 1, memory_order_relaxed) != 1 || value != 0) {
		puts("FAIL");
		return 1;
	}
	if ((value += 3) != 3 || value != 3) {
		puts("FAIL");
		return 1;
	}
	if (atomic_fetch_or(&value, 4) != 3 || atomic_fetch_and(&value, 6) != 7 ||
	    atomic_fetch_xor(&value, 3) != 6 || value != 5) {
		puts("FAIL");
		return 1;
	}
	if (atomic_exchange(&value, 9) != 5 || value != 9) {
		puts("FAIL");
		return 1;
	}
	{
		int expected = 9;
		if (!atomic_compare_exchange_strong(&value, &expected, 11) || value != 11) {
			puts("FAIL");
			return 1;
		}
		expected = 9;
		if (atomic_compare_exchange_strong(&value, &expected, 13) || expected != 11) {
			puts("FAIL");
			return 1;
		}
	}
	{
		atomic_flag flag = ATOMIC_FLAG_INIT;
		if (atomic_flag_test_and_set(&flag) || !atomic_flag_test_and_set(&flag)) {
			puts("FAIL");
			return 1;
		}
		atomic_flag_clear(&flag);
		if (atomic_flag_test_and_set(&flag)) {
			puts("FAIL");
			return 1;
		}
	}
	puts("PASS");
	return 0;
}
