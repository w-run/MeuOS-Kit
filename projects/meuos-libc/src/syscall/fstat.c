#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"
#define LINUX_SYS_FSTAT 5
int fstat(int descriptor, struct stat *result) { long value = __syscall2(LINUX_SYS_FSTAT, descriptor, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
