#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_GETPPID 110

pid_t
getppid(void)
{
	return (pid_t)__syscall0(LINUX_SYS_GETPPID);
}
