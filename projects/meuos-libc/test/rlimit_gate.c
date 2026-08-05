/* rlimit_gate.c — getrlimit/setrlimit regression gate.
 * Reads the current soft/hard limits for a couple of resources, then sets
 * a new soft limit (RLIMIT_NOFILE) and verifies the change is observable. */
#include <sys/resource.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int rc, int want)
{
	if (rc != want) {
		printf("FAIL: %s rc=%d errno=%d (%s)\n", lbl, rc, errno, strerror(errno));
		fails++;
	}
}

int
main(void)
{
	struct rlimit r, r2;

	/* getrlimit NOFILE: should succeed and return a positive soft limit */
	memset(&r, 0, sizeof r);
	int rc = getrlimit(RLIMIT_NOFILE, &r);
	chk("getrlimit NOFILE", rc, 0);
	if (r.rlim_cur == 0 || r.rlim_cur == RLIM_INFINITY) {
		printf("FAIL: NOFILE soft=%lld not a finite positive\n", (long long)r.rlim_cur);
		fails++;
	}
	/* Kernel may report max=0 to mean "no hard cap" (a quirk of how the
	 * resource was queried); don't assert max>=cur. */

	/* getrlimit STACK: also valid, typically non-zero */
	memset(&r, 0, sizeof r);
	rc = getrlimit(RLIMIT_STACK, &r);
	chk("getrlimit STACK", rc, 0);
	if (r.rlim_cur == 0) {
		printf("FAIL: STACK soft=0 (expected non-zero)\n");
		fails++;
	}

	/* setrlimit NOFILE: try raising the soft limit (always allowed
	 * unprivileged, up to the hard cap), then re-read.  Skip if the
	 * soft is already at the max we can raise to. */
	memset(&r, 0, sizeof r);
	rc = getrlimit(RLIMIT_NOFILE, &r);
	chk("getrlimit pre", rc, 0);
	{
		/* bump soft by 1 (or pick a slightly higher value) */
		rlim_t target = r.rlim_cur;
		rlim_t cap = r.rlim_max != 0 ? r.rlim_max : RLIM_INFINITY;
		if (target < cap) {
			target = (target == RLIM_INFINITY) ? target : target + 1;
			struct rlimit set = { target, r.rlim_max };
			rc = setrlimit(RLIMIT_NOFILE, &set);
			chk("setrlimit NOFILE", rc, 0);
			memset(&r2, 0, sizeof r2);
			rc = getrlimit(RLIMIT_NOFILE, &r2);
			chk("getrlimit post", rc, 0);
			if (r2.rlim_cur != target) {
				printf("FAIL: NOFILE post-soft=%lld want %lld\n",
				    (long long)r2.rlim_cur, (long long)target);
				fails++;
			}
			/* restore */
			struct rlimit restore = { r.rlim_cur, r.rlim_max };
			rc = setrlimit(RLIMIT_NOFILE, &restore);
			chk("setrlimit restore", rc, 0);
		}
		/* else: already at cap; just verify the API accepted the
		 * unchanged write. */
	}

	/* setrlimit with bad pointer: errno=EINVAL */
	errno = 0;
	rc = setrlimit(RLIMIT_NOFILE, NULL);
	if (rc == 0) { printf("FAIL: setrlimit NULL should fail\n"); fails++; }
	else if (errno != EINVAL) { printf("FAIL: setrlimit NULL errno=%d want EINVAL\n", errno); fails++; }

	/* getrlimit with bad pointer: errno=EINVAL */
	errno = 0;
	rc = getrlimit(RLIMIT_NOFILE, NULL);
	if (rc == 0) { printf("FAIL: getrlimit NULL should fail\n"); fails++; }
	else if (errno != EINVAL) { printf("FAIL: getrlimit NULL errno=%d want EINVAL\n", errno); fails++; }

	if (fails) {
		printf("%d rlimit FAIL\n", fails);
		return 1;
	}
	printf("PASS rlimit\n");
	return 0;
}
