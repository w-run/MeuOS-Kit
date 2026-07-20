#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_RENAME 82

int
rename(const char *old_path, const char *new_path)
{
	long result = __syscall2(LINUX_SYS_RENAME, (long)old_path, (long)new_path);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
