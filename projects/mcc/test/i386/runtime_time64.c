/* i386 time64 + stat regression test.
 *
 * Verifies that 64-bit time_t works correctly on i386 (where time_t
 * is long long = Kl, decomposed via two movl).  Also tests stat()
 * which returns 64-bit timestamp fields — the canonical case where
 * Kl load clobbered Kw st_mode before the push/pop fix.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

int main(void)
{
	/* clock_gettime with 64-bit time_t. */
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		printf("FAIL: clock_gettime\n");
		return 1;
	}
	if (ts.tv_sec < 1000000000LL) {
		printf("FAIL: tv_sec=%lld too small\n", (long long)ts.tv_sec);
		return 2;
	}

	/* time() with 64-bit time_t. */
	time_t now = time(NULL);
	if (now < 1000000000LL) {
		printf("FAIL: time()=%lld too small\n", (long long)now);
		return 3;
	}

	/* stat() with 64-bit timestamp fields — the Kl clobber reproducer. */
	struct stat st;
	if (stat("/proc/self/exe", &st) != 0) {
		printf("FAIL: stat\n");
		return 4;
	}
	/* st_mode must be 0100755 or 0100644 (regular file). */
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
