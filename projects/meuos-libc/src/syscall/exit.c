#include <unistd.h>
#include "../internal/syscall.h"

/* Linux syscall number for exit.  _exit intentionally bypasses all stdio
 * and atexit handling; the full exit(3) belongs to stdlib later. */
/* x86_64 exit number; __syscall_number() (in internal/syscall.h)
 * translates it to the i386 exit syscall (1).  Do NOT use the raw i386
 * number 1 here: 1 is "write" in the x86_64 numbering that
 * __syscall1() passes through __syscall_number(), so it would be
 * mistranslated to i386 write (4). */
/* exit (x86_64=60, i386=1): exits only the calling thread (in a
 * CLONE_THREAD group) and still triggers CLONE_CHILD_CLEARTID, which
 * zeroes the child tid and wakes the parent's futex in thrd_join.
 * Do NOT use exit_group (231/252): it terminates the whole thread
 * group, killing the parent mid-thrd_join.  Do NOT use the raw i386
 * number 1 here: 1 is "write" in the x86_64 numbering that
 * __syscall1() passes through __syscall_number(), so it would be
 * mistranslated to i386 write (4). */
#define LINUX_SYS_EXIT 60

_Noreturn void
_exit(int status)
{
	__syscall1(LINUX_SYS_EXIT, status);
	for (;;)
		;
}
