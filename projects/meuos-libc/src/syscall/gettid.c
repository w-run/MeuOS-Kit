#include <unistd.h>
#include "../internal/syscall.h"

/* Linux x86_64 __NR_gettid.  Unlike most wrappers this syscall has no
 * meaningful failure path: the kernel always returns the caller's TID. */
#define LINUX_SYS_GETTID 186

pid_t
gettid(void)
{
	return (pid_t)__syscall0(LINUX_SYS_GETTID);
}
