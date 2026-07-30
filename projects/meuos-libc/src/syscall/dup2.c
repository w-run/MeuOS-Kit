#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64) || defined(__arm__)
/* aarch64 没有 dup2(33)，改用 dup3(oldfd, newfd, flags)。dup3 要求
 * oldfd != newfd，否则返回 EINVAL；dup2 在 old==new 时直接返回该 fd。
 * 这里显式处理这一边界以保持 dup2 语义。flags=0 等价无 CLOEXEC。 */
#define LINUX_SYS_DUP3 292

int
dup2(int old_descriptor, int new_descriptor)
{
	long result;

	if (old_descriptor == new_descriptor) {
		/* dup2 允许 old==new 并直接返回，dup3 会拒绝。 */
		return new_descriptor;
	}
	result = __syscall3(LINUX_SYS_DUP3, old_descriptor, new_descriptor, 0);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
#else
#define LINUX_SYS_DUP2 33

int
dup2(int old_descriptor, int new_descriptor)
{
	long result = __syscall2(LINUX_SYS_DUP2, old_descriptor, new_descriptor);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
#endif
