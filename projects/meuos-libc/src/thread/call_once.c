/* thread/call_once.c -- C11 call_once() primitive.
 *
 * Implements the once_flag state machine (0=idle, 1=in-progress,
 * 2=done). The first caller runs the function; concurrent callers
 * spin on a futex until the state flips to 2. */

#include <stdatomic.h>
#include <threads.h>
#include "../internal/syscall.h"
#include "internal.h"

void
call_once(once_flag *flag, void (*function)(void))
{
	int expected;

	if (!flag || !function)
		return;
	for (;;) {
		if (atomic_load(&flag->state) == 2)
			return;
		expected = 0;
		if (atomic_compare_exchange_strong(&flag->state, &expected, 1)) {
			function();
			atomic_store(&flag->state, 2);
			__syscall6(LINUX_SYS_FUTEX, (long)&flag->state, 1, 0x7fffffff, 0, 0, 0);
			return;
		}
		if (expected == 1)
			__syscall6(LINUX_SYS_FUTEX, (long)&flag->state, FUTEX_WAIT, 1, 0, 0, 0);
	}
}
