#include <errno.h>
#include <sys/socket.h>
#include "../internal/syscall.h"
#define LINUX_SYS_SOCKET 41
int socket(int family, int type, int protocol) { long value = __syscall3(LINUX_SYS_SOCKET, family, type, protocol); if (__syscall_error(value)) { errno = (int)-value; return -1; } return (int)value; }
