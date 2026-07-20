#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_SYMLINK 88
int symlink(const char *target, const char *path) { long value = __syscall2(LINUX_SYS_SYMLINK, (long)target, (long)path); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
