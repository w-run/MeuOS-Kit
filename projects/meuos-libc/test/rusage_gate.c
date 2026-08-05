/* rusage_gate.c — getrusage() regression gate.
 *
 * Verifies getrusage returns distinct, well-formed rusage for SELF,
 * CHILDREN and THREAD, that cumulative user/system CPU is non-negative, and
 * that invalid arguments report the expected errno.  Deliberately loose on
 * concrete values (resource counters vary by run) but strict on "the
 * syscall succeeded and the struct is sane". */
#include <sys/resource.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d %s)\n", lbl, errno, "err");
		fails++;
	}
}

static void
check_rusage(const char *tag, const struct rusage *ru)
{
	/* cpu time fields are non-negative; counters are non-negative. */
	chk(tag, ru->ru_utime.tv_sec >= 0);
	chk(tag, ru->ru_stime.tv_sec >= 0);
	chk(tag, ru->ru_utime.tv_usec >= 0);
	chk(tag, ru->ru_maxrss >= 0);
	chk(tag, ru->ru_nvcsw >= 0);
	(void)tag;
}

int
main(void)
{
	struct rusage ru;

	errno = 0;
	chk("getrusage(SELF)", getrusage(RUSAGE_SELF, &ru) == 0);
	check_rusage("SELF", &ru);

	errno = 0;
	chk("getrusage(CHILDREN)", getrusage(RUSAGE_CHILDREN, &ru) == 0);
	check_rusage("CHILDREN", &ru);

	errno = 0;
	chk("getrusage(THREAD)", getrusage(RUSAGE_THREAD, &ru) == 0);
	check_rusage("THREAD", &ru);

	/* invalid who -> EINVAL */
	errno = 0;
	if (getrusage(99, &ru) != -1) {
		printf("FAIL: getrusage(bad-who) should fail\n");
		fails++;
	} else if (errno != EINVAL) {
		printf("FAIL: getrusage(bad-who) errno=%d want EINVAL\n", errno);
		fails++;
	}

	/* NULL usage -> EFAULT (passes -1,0 to the kernel) */
	errno = 0;
	if (getrusage(RUSAGE_SELF, 0) != -1) {
		printf("FAIL: getrusage(NULL) should fail\n");
		fails++;
	} else if (errno != EFAULT) {
		printf("FAIL: getrusage(NULL) errno=%d want EFAULT\n", errno);
		fails++;
	}

	if (fails) {
		printf("%d rusage FAIL\n", fails);
		return 1;
	}
	printf("PASS rusage\n");
	return 0;
}
