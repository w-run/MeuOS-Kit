#include <errno.h>
#include <stdint.h>
#include <time.h>
#include "../internal/syscall.h"

#if defined(__i386__)
struct meuos_kernel_timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
};

int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
	struct meuos_kernel_timespec64 req64, rem64;
	long value;

	if (!request) {
		errno = EINVAL;
		return -1;
	}
	req64.tv_sec = request->tv_sec;
	req64.tv_nsec = request->tv_nsec;
	value = __syscall4(407, CLOCK_REALTIME, 0, (long)&req64,
		remaining ? (long)&rem64 : 0);
	if (__syscall_error(value)) {
		if (remaining) {
			remaining->tv_sec = rem64.tv_sec;
			remaining->tv_nsec = (long)rem64.tv_nsec;
		}
		errno = (int)-value;
		return -1;
	}
	return 0;
}
#else
int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
	long value = __syscall2(35, (long)request, (long)remaining);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return 0;
}
#endif
