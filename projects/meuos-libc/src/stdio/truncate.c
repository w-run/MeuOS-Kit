/* stdio/truncate.c -- POSIX.1-2008 truncate()/ftruncate() wrappers.
 *
 * ftruncate(fd, length) truncates an already-open file; truncate(path,
 * length) opens nothing itself, taking a path.  Both are thin syscall
 * passes-through isolated here so the syscall number constants don't leak
 * into the rest of stdio/.  Zero GNU dependency. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_TRUNCATE
#define LINUX_SYS_TRUNCATE 76
#endif
#ifndef LINUX_SYS_FTRUNCATE
#define LINUX_SYS_FTRUNCATE 77
#endif

int
truncate(const char *path, off_t length)
{
	long result = __syscall2(LINUX_SYS_TRUNCATE, (long)path, length);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
ftruncate(int fd, off_t length)
{
	long result = __syscall2(LINUX_SYS_FTRUNCATE, fd, length);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
