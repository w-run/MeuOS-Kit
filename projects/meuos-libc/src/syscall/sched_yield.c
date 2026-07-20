#include <errno.h>
#include <sched.h>
#include "../internal/syscall.h"
#define LINUX_SYS_SCHED_YIELD 24
int sched_yield(void) { long value = __syscall0(LINUX_SYS_SCHED_YIELD); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
