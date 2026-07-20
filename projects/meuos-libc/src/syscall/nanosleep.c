#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"
#define LINUX_SYS_NANOSLEEP 35
int nanosleep(const struct timespec *request, struct timespec *remaining) { long value = __syscall2(LINUX_SYS_NANOSLEEP, (long)request, (long)remaining); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
