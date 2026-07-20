/* stdio/truncate.c -- ftruncate() wrapper.
 *
 * ftruncate is the only stdio-adjacent function that needs the syscall
 * gate directly (the file descriptor stays in user code; we just pass
 * the syscall through). Isolated here so the syscall number constant
 * doesn't leak into the rest of stdio/. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* ftruncate syscall number (x86_64). */
#define LINUX_SYS_FTRUNCATE 77

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
