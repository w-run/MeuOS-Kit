#include <errno.h>
#include <sys/time.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETTIMEOFDAY 96
int gettimeofday(struct timeval *tv, void *tz) {
	long r = __syscall2(LINUX_SYS_GETTIMEOFDAY, (long)tv, (long)tz);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
