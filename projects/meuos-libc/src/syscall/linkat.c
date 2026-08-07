/* syscall/linkat.c — linkat (x86_64: 265) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_LINKAT 265

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags)
{
	long r = __syscall5(LINUX_SYS_LINKAT, olddirfd, (long)oldpath, newdirfd, (long)newpath, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}