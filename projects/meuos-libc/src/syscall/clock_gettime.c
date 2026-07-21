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
clock_gettime(clockid_t clock, struct timespec *result)
{
	struct meuos_kernel_timespec64 kernel_result;
	long value;

	if (!result) {
		errno = EINVAL;
		return -1;
	}
	value = __syscall2(403, clock, (long)&kernel_result);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	result->tv_sec = kernel_result.tv_sec;
	result->tv_nsec = (long)kernel_result.tv_nsec;
	return 0;
}
#else
int
clock_gettime(clockid_t clock, struct timespec *result)
{
	long value = __syscall2(228, clock, (long)result);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return 0;
}
#endif
