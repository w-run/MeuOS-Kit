/* syscall/readlinkat.c — readlinkat (x86_64: 267) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_READLINKAT 267

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
	long r = __syscall4(LINUX_SYS_READLINKAT, dirfd, (long)path, (long)buf, bufsiz);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (ssize_t)r;
}