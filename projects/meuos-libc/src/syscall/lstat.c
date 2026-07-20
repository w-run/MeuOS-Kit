#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"
#define LINUX_SYS_LSTAT 6
int lstat(const char *path, struct stat *result) { long value = __syscall2(LINUX_SYS_LSTAT, (long)path, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
