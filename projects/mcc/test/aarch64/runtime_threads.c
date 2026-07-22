/* aarch64 threads + atomics regression test.
 *
 * Mirrors the canonical AGENTS.md §6 acceptance program: two C11
 * threads each increment a shared _Atomic int 1000 times, and the
 * joined counter must read 2000.  On aarch64 this exercises:
 *   - thrd_create / thrd_join (clone + futex + TLS setup via TPIDR_EL0)
 *   - _Atomic int fetch-add (atomic.S ldadd/stlr helpers)
 *   - per-thread stack mmap + __meuos_tls_alloc/free (variant I)
 *   - libatomic-meuos.a runtime linking
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

_Atomic int counter = 0;

static int
thread_func(void *arg)
{
	(void)arg;
	for (int i = 0; i < 1000; i++)
		counter++;
	return 0;
}

int
main(void)
{
	thrd_t t1, t2;

	if (thrd_create(&t1, thread_func, NULL) != thrd_success) {
		printf("FAIL: thrd_create t1\n");
		return 1;
	}
	if (thrd_create(&t2, thread_func, NULL) != thrd_success) {
		printf("FAIL: thrd_create t2\n");
		return 2;
	}
	thrd_join(t1, NULL);
	thrd_join(t2, NULL);

	if (counter != 2000) {
		printf("FAIL: counter=%d expected 2000\n", counter);
		return 3;
	}
	printf("OK: threads+atomic counter=2000\n");
	return 0;
}
