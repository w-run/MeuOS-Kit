#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_LINK 86
int link(const char *old_path, const char *new_path) { long value = __syscall2(LINUX_SYS_LINK, (long)old_path, (long)new_path); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
