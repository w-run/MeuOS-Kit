#include <errno.h>
#include <sys/socket.h>
#include "../internal/syscall.h"
#define LINUX_SYS_CONNECT 42
int connect(int descriptor, const struct sockaddr *address, socklen_t length) { long value = __syscall3(LINUX_SYS_CONNECT, descriptor, (long)address, length); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
