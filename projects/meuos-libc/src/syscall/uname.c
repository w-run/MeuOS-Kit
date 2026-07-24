#include <errno.h>
#include <sys/utsname.h>
#include "../internal/syscall.h"

#define LINUX_SYS_UNAME 63

int
uname(struct utsname *name)
{
	long result = __syscall1(LINUX_SYS_UNAME, (long)name);
	if (__syscall_error(result)) {
		errno = -result;
		return -1;
	}
	return 0;
}
