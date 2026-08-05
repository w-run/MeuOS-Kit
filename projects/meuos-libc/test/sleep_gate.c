/* sleep_gate.c — sleep/usleep regression gate.
 * Verifies sleep(0)/usleep(1) return 0 immediately and that usleep() actually
 * suspends for the requested (short) interval, using a generous lower bound
 * to stay robust.  A true sleep(N) for whole seconds is not wall-clock gated
 * here to avoid adding N seconds of runtime to `make check`. */
#include <unistd.h>
#include <time.h>
#include <stdio.h>

static int fails;

int
main(void)
{
	unsigned int r;

	r = sleep(0);
	if (r != 0) { printf("FAIL: sleep(0)=%u want 0\n", r); fails++; }

	if (usleep(1) != 0) { printf("FAIL: usleep(1)\n"); fails++; }

	/* ~100ms sleep must take at least ~60ms (generous lower bound). */
	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (usleep(100000) != 0) { printf("FAIL: usleep(100000)\n"); fails++; }
		clock_gettime(CLOCK_MONOTONIC, &t1);
		long long ns = (long long)(t1.tv_sec - t0.tv_sec) * 1000000000LL
		             + (long long)(t1.tv_nsec - t0.tv_nsec);
		long long ms = ns / 1000000;
		if (ms < 60) { printf("FAIL: usleep(100000) elapsed=%lldms want>60\n", ms); fails++; }
	}

	if (fails) {
		printf("%d sleep FAIL\n", fails);
		return 1;
	}
	printf("PASS sleep\n");
	return 0;
}
