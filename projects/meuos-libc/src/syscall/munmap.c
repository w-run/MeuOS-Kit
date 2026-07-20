#include <errno.h>
#include <sys/mman.h>
#include "../internal/syscall.h"
#define LINUX_SYS_MUNMAP 11
int munmap(void *address, size_t length) { long result = __syscall2(LINUX_SYS_MUNMAP, (long)address, length); if (__syscall_error(result)) { errno = (int)-result; return -1; } return 0; }
