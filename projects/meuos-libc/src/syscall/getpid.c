#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_GETPID 39

pid_t
getpid(void)
{
	return (pid_t)__syscall0(LINUX_SYS_GETPID);
}
