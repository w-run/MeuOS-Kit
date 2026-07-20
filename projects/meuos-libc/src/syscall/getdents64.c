#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETDENTS64 217
ssize_t getdents64(int descriptor, void *buffer, size_t size) { long value = __syscall3(LINUX_SYS_GETDENTS64, descriptor, (long)buffer, size); if (__syscall_error(value)) { errno = (int)-value; return -1; } return (ssize_t)value; }
