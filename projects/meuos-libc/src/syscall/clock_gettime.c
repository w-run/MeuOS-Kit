#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"
#define LINUX_SYS_CLOCK_GETTIME 228
int clock_gettime(clockid_t clock, struct timespec *result) { long value = __syscall2(LINUX_SYS_CLOCK_GETTIME, clock, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
