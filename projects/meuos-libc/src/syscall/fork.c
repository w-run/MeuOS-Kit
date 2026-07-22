#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__)
/* aarch64 没有 fork(57)，改用 clone(flags, stack, ptid, tls, ctid)。
 * flags=SIGCHLD(17) + 其余参数为 0 即等价 fork()。clone 需要 5 个参数，
 * 用 __syscall6 多传一个 0，第 6 个参数被 aarch64 syscall ABI 忽略。
 * child_stack=0 让内核按 fork 语义复制栈。 */
#define LINUX_SYS_CLONE 56
#define LINUX_SIGCHLD   17

pid_t
fork(void)
{
	long result = __syscall6(LINUX_SYS_CLONE, LINUX_SIGCHLD, 0, 0, 0, 0, 0);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (pid_t)result;
}
#else
#define LINUX_SYS_FORK 57

pid_t
fork(void)
{
	long result = __syscall0(LINUX_SYS_FORK);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (pid_t)result;
}
#endif
