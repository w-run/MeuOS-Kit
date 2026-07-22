/* aarch64 time64 + stat regression test.
 *
 * aarch64 has 64-bit time_t natively (no decomposition needed), but
 * this still exercises: clock_gettime() with 64-bit timespec, time()
 * with 64-bit time_t, and stat() returning 64-bit timestamp fields
 * (the path that originally died in aarch64_emit.c with "invalid
 * class" because Ostorel was matched against cls=Kw).
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

int
main(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		printf("FAIL: clock_gettime\n");
		return 1;
	}
	if (ts.tv_sec < 1000000000LL) {
		printf("FAIL: tv_sec=%lld too small\n", (long long)ts.tv_sec);
		return 2;
	}

	time_t now = time(NULL);
	if (now < 1000000000LL) {
		printf("FAIL: time()=%lld too small\n", (long long)now);
		return 3;
	}

	/* stat() with 64-bit timestamp fields.  On aarch64 this exercises
	 * the Ostorel Ki omap fix and the aarch64 newfstatat(79) wrapper
	 * in src/syscall/fstat.c.  /proc/self/exe is the test binary
	 * itself, which is a regular file present in any Linux. */
	struct stat st;
	if (stat("/proc/self/exe", &st) != 0) {
		printf("FAIL: stat\n");
		return 4;
	}
	if ((st.st_mode & 0xF000) != 0x8000) {
		printf("FAIL: st_mode=%o not regular\n", st.st_mode);
		return 5;
	}
	if (st.st_size <= 0) {
		printf("FAIL: st_size=%lld\n", (long long)st.st_size);
		return 6;
	}
	if (st.st_atime < 1000000000LL) {
		printf("FAIL: st_atime=%lld\n", (long long)st.st_atime);
		return 7;
	}
	if (st.st_mtime < 1000000000LL) {
		printf("FAIL: st_mtime=%lld\n", (long long)st.st_mtime);
		return 8;
	}

	printf("OK: time64+stat regression passed\n");
	return 0;
}
