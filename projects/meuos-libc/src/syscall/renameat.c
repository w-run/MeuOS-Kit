/* syscall/renameat.c — renameat (x86_64: 264) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_RENAMEAT 264

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
	long r = __syscall4(LINUX_SYS_RENAMEAT, olddirfd, (long)oldpath, newdirfd, (long)newpath);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}