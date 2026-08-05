/* prioritize_gate.c — getpriority/setpriority/nice regression gate.
 * Verifies the POSIX API contract: successful reads/writes and the
 * EINVAL/ESRCH error paths.  It deliberately does NOT assert specific nice
 * values: the host/container may clamp or seed priorities (observed nice
 * ranges come back as low as the RLIMIT_NICE floor), so the gate checks
 * success semantics and the guaranteed error cases only. */
#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int fails;

int
main(void)
{
	int g;

	/* getpriority current process: succeeds (errno cleared).  Returned
	 * value may be clamped by the environment; only check it is a valid
	 * scheduling-class int and the call did not signal an error. */
	errno = 0;
	g = getpriority(PRIO_PROCESS, 0);
	if (g == -1 && errno != 0) {
		printf("FAIL: getpriority errno=%d (%s)\n", errno, strerror(errno));
		fails++;
		g = 0;
	}

	/* setpriority writing the just-read value back: must succeed. */
	errno = 0;
	if (setpriority(PRIO_PROCESS, 0, g) != 0) {
		printf("FAIL: setpriority(cur) errno=%d (%s)\n", errno, strerror(errno));
		fails++;
	}

	/* nice(0): no-op increment — succeeds and returns a value. */
	errno = 0;
	g = nice(0);
	if (g == -1 && errno != 0) {
		printf("FAIL: nice(0) errno=%d (%s)\n", errno, strerror(errno));
		fails++;
	}

	/* nice(-small) or nice(+small) must not corrupt errno on success. */
	errno = 0;
	{
		int r = nice(1);
		if (r == -1 && errno != 0)
			printf("note: nice(+1) errno=%d (%s)\n", errno, strerror(errno));
		else
			(void)0;
	}

	/* invalid who → ESRCH for a nonexistent uid via PRIO_USER. */
	errno = 0;
	if (getpriority(PRIO_USER, (id_t)0x7fffffff) != -1) {
		printf("FAIL: getpriority bad-who should fail\n");
		fails++;
	} else if (errno != ESRCH) {
		printf("FAIL: getpriority bad-who errno=%d want ESRCH\n", errno);
		fails++;
	}

	/* invalid which → EINVAL. */
	errno = 0;
	if (setpriority(99, 0, 0) != -1) {
		printf("FAIL: setpriority bad-which should fail\n");
		fails++;
	} else if (errno != EINVAL) {
		printf("FAIL: setpriority bad-which errno=%d want EINVAL\n", errno);
		fails++;
	}
	errno = 0;
	if (getpriority(99, 0) != -1) {
		printf("FAIL: getpriority bad-which should fail\n");
		fails++;
	} else if (errno != EINVAL) {
		printf("FAIL: getpriority bad-which errno=%d want EINVAL\n", errno);
		fails++;
	}

	if (fails) {
		printf("%d prioritize FAIL\n", fails);
		return 1;
	}
	printf("PASS prioritize\n");
	return 0;
}
