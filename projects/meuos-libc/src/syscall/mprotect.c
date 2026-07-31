#include <errno.h>
#include <sys/mman.h>
#include "../internal/syscall.h"
#define LINUX_SYS_MPROTECT 10
int mprotect(const void *addr, size_t len, int prot) { long result = __syscall3(LINUX_SYS_MPROTECT, (long)addr, len, prot); if (__syscall_error(result)) { errno = (int)-result; return -1; } return 0; }
