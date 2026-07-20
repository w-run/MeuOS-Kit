#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"
#define LINUX_SYS_CHMOD 90
int chmod(const char *path, mode_t mode) { long value = __syscall2(LINUX_SYS_CHMOD, (long)path, mode); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
