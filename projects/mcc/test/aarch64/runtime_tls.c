/* aarch64 TLS (Thread-Local Storage) regression test.
 *
 * aarch64 ELF TLS uses variant I: static TLS lives at non-negative
 * offsets from TPIDR_EL0, which the main thread programs via
 * __meuos_set_tls() and child threads inherit via CLONE_SETTLS.
 * mcc emits local-exec TLS access as:
 *
 *     mrs   R, tpidr_el0
 *     add   R, R, #:tprel_hi12:S, lsl #12
 *     add   R, R, #:tprel_lo12_nc:S
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>
#include <threads.h>

static _Thread_local int per_thread = 100;
static _Thread_local long long per_thread_ll = 0;

static int
writer(void *arg)
{
	(void)arg;
	per_thread = 0xfeed;
	per_thread_ll = 0x123456789aLL;
	return 0;
}

int
main(void)
{
	thrd_t t;
	int rc;

	/* Main-thread LE access via mrs tpidr_el0. */
	if (per_thread != 100) {
		printf("FAIL: main per_thread=%d\n", per_thread);
		return 1;
	}

	per_thread = 7;
	if (per_thread != 7) {
		printf("FAIL: main per_thread=%d after write\n", per_thread);
		return 2;
	}

	/* Spawn a thread that writes its own TLS; main must be unaffected. */
	if (thrd_create(&t, writer, NULL) != thrd_success) {
		printf("FAIL: thrd_create\n");
		return 3;
	}
	thrd_join(t, &rc);
	if (rc != 0) {
		printf("FAIL: writer rc=%d\n", rc);
		return 4;
	}

	if (per_thread != 7) {
		printf("FAIL: main per_thread=%d after thread\n", per_thread);
		return 5;
	}

	/* 64-bit TLS slot (Kl store/load via the Ostorel fix). */
	per_thread_ll = 0x1000000042LL;
	if (per_thread_ll != 0x1000000042LL) {
		printf("FAIL: per_thread_ll=%lld\n", (long long)per_thread_ll);
		return 6;
	}

	printf("OK: TLS regression passed\n");
	return 0;
}
