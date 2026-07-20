#include <errno.h>
#include <sys/socket.h>
#include "../internal/syscall.h"
#define LINUX_SYS_LISTEN 50
int listen(int descriptor, int backlog) { long value = __syscall2(LINUX_SYS_LISTEN, descriptor, backlog); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
