#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_ACCESS 21
int access(const char *path, int mode) { long value = __syscall2(LINUX_SYS_ACCESS, (long)path, mode); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
