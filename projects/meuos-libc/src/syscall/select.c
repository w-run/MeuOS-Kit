#include <errno.h>
#include <sys/select.h>
#include "../internal/syscall.h"

/* 内部号统一用 x86_64 的 SYS_select=23，由 syscall.h 翻译表转成各架构
 * 原生号。asm-generic 架构（aarch64/riscv64/loongarch64）没有 select(2)，
 * 翻译表把 23 映射到 pselect6，这里相应地把 timeval 转成 timespec 传入。 */
#define LINUX_SYS_SELECT 23

#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
int select(int n, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv)
{
	struct timespec ts, *tsp = 0;
	if (tv) {
		ts.tv_sec = tv->tv_sec;
		ts.tv_nsec = tv->tv_usec * 1000;
		tsp = &ts;
	}
	long result = __syscall6(LINUX_SYS_SELECT, n, (long)rfds, (long)wfds,
	                         (long)efds, (long)tsp, 0);
	if (__syscall_error(result)) { errno = (int)-result; return -1; }
	return (int)result;
}
#else
/* x86_64/i386/arm 有 5 参数 select(2)。timeval 拆成 long[2] 传递（musl 做法），
 * 避免本 libc 的 struct timeval 布局与内核期望不一致（i386 下 time_t 是 64 位，
 * 而内核 select 期望 {long,long}）。多余的第 6 个参数被内核忽略。 */
int select(int n, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv)
{
	long ts[2], *tsp = 0;
	if (tv) {
		ts[0] = tv->tv_sec;
		ts[1] = tv->tv_usec;
		tsp = ts;
	}
	long result = __syscall6(LINUX_SYS_SELECT, n, (long)rfds, (long)wfds,
	                         (long)efds, (long)tsp, 0);
	if (__syscall_error(result)) { errno = (int)-result; return -1; }
	return (int)result;
}
#endif
