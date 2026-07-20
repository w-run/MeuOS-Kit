#include <stdatomic.h>

int
main(void)
{
	atomic_thread_fence(memory_order_seq_cst);
	atomic_signal_fence(memory_order_acquire);
	return 0;
}
