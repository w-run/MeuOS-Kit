#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_IOCTL 16
#define TCGETS 0x5401
int isatty(int fd) {
	long r = __syscall3(LINUX_SYS_IOCTL, fd, TCGETS, 0);
	return r == 0 ? 1 : 0;
}
