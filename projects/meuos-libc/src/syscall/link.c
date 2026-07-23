#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
/* aarch64 没有 link(2)，改用 linkat(AT_FDCWD, old, AT_FDCWD, new, 0)。
 * linkat 有 5 个参数，syscall ABI 忽略多余的第 6 个参数，故用 __syscall6。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_LINKAT 265
int link(const char *old_path, const char *new_path) {
	long value = __syscall6(LINUX_SYS_LINKAT, AT_FDCWD, (long)old_path, AT_FDCWD, (long)new_path, 0, 0);
	if (__syscall_error(value)) { errno = (int)-value; return -1; }
	return 0;
}
#else
#define LINUX_SYS_LINK 86
int link(const char *old_path, const char *new_path) { long value = __syscall2(LINUX_SYS_LINK, (long)old_path, (long)new_path); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
