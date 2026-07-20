#include <unistd.h>
#include "../internal/syscall.h"

/* Linux syscall number for exit.  _exit intentionally bypasses all stdio
 * and atexit handling; the full exit(3) belongs to stdlib later. */
#if defined(__i386__)
#define LINUX_SYS_EXIT 1
#else
#define LINUX_SYS_EXIT 60
#endif

_Noreturn void
_exit(int status)
{
	__syscall1(LINUX_SYS_EXIT, status);
	for (;;)
		;
}
