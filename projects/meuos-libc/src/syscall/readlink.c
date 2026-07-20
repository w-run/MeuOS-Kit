#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_READLINK 89
ssize_t readlink(const char *path, char *buffer, size_t size) { long value = __syscall3(LINUX_SYS_READLINK, (long)path, (long)buffer, size); if (__syscall_error(value)) { errno = (int)-value; return -1; } return (ssize_t)value; }
