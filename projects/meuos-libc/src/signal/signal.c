#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* Signal-return trampoline (see src/x86_64/sigreturn.S). */
extern void __meuos_restore_rt(void);

/* Stable internal syscall IDs; src/internal/syscall.h translates these
 * to the native i386 numbers when building the 32-bit target. */
#define LINUX_SYS_RT_SIGACTION    13
#define LINUX_SYS_RT_SIGPROCMASK  14
#define LINUX_SYS_RT_SIGPENDING  127
#define LINUX_SYS_RT_SIGSUSPEND  130
#define LINUX_SYS_SIGALTSTACK    131
#define LINUX_SYS_KILL            62
#define LINUX_SYS_TGKILL         234

/* Kernel layout expected by rt_sigaction on x86_64:
 *   handler, flags, restorer, mask(8 bytes).  This differs from the
 *   userspace struct sigaction, so we translate here rather than expose
 *   the kernel shape to applications. */
struct k_sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	sigset_t mask;
};

/* rt_sigaction takes the sigset size as its fourth argument. */
static int
do_sigaction(int signum, const struct k_sigaction *act,
    struct k_sigaction *oldact)
{
	long result = __syscall4(LINUX_SYS_RT_SIGACTION, signum,
	    (long)act, (long)oldact, sizeof(sigset_t));
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
sigaction(int signum, const struct sigaction *act,
    struct sigaction *oldact)
{
	struct k_sigaction kact, kold;

	if (signum <= 0 || signum >= NSIG) {
		errno = EINVAL;
		return -1;
	}
	if (act) {
		kact.handler = act->sa_handler;
		kact.flags = (unsigned long)act->sa_flags | SA_RESTORER;
		kact.restorer = act->sa_restorer ? act->sa_restorer
		    : __meuos_restore_rt;
		kact.mask = act->sa_mask;
		/* Avoid clearing the handler's own signal while it runs. */
		kact.mask |= ((sigset_t)1 << (signum - 1));
	}
	if (do_sigaction(signum, act ? &kact : 0,
	    oldact ? &kold : 0) < 0)
		return -1;
	if (oldact) {
		oldact->sa_handler = kold.handler;
		oldact->sa_flags = (int)kold.flags;
		oldact->sa_restorer = kold.restorer;
		oldact->sa_mask = kold.mask;
	}
	return 0;
}

void (*signal(int signum, void (*handler)(int)))(int)
{
	struct sigaction act, old;

	act.sa_handler = handler;
	act.sa_flags = 0;
	act.sa_restorer = (void (*)(void))0;
	act.sa_mask = 0;
	if (sigaction(signum, &act, &old) < 0)
		return SIG_ERR;
	return old.sa_handler;
}

int
sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	long result;

	if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK) {
		errno = EINVAL;
		return -1;
	}
	result = __syscall4(LINUX_SYS_RT_SIGPROCMASK, how, (long)set,
	    (long)oldset, sizeof(sigset_t));
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
sigpending(sigset_t *set)
{
	long result = __syscall4(LINUX_SYS_RT_SIGPENDING, (long)set,
	    sizeof(sigset_t), 0, 0);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
sigsuspend(const sigset_t *mask)
{
	long result = __syscall3(LINUX_SYS_RT_SIGSUSPEND, (long)mask,
	    sizeof(sigset_t), 0);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return -1;
}

int
sigaltstack(const stack_t *ss, stack_t *oss)
{
	long result = __syscall2(LINUX_SYS_SIGALTSTACK, (long)ss, (long)oss);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
kill(pid_t pid, int sig)
{
	long result = __syscall2(LINUX_SYS_KILL, pid, sig);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
tgkill(pid_t tgid, pid_t tid, int sig)
{
	long result = __syscall3(LINUX_SYS_TGKILL, tgid, tid, sig);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}

int
raise(int sig)
{
	return tgkill(getpid(), gettid(), sig);
}

int
sigemptyset(sigset_t *set)
{
	*set = 0;
	return 0;
}

int
sigfillset(sigset_t *set)
{
	*set = (unsigned long)-1;
	return 0;
}

int
sigaddset(sigset_t *set, int signum)
{
	if (signum <= 0 || signum >= NSIG) {
		errno = EINVAL;
		return -1;
	}
	*set |= ((sigset_t)1 << (signum - 1));
	return 0;
}

int
sigdelset(sigset_t *set, int signum)
{
	if (signum <= 0 || signum >= NSIG) {
		errno = EINVAL;
		return -1;
	}
	*set &= ~((sigset_t)1 << (signum - 1));
	return 0;
}

int
sigismember(const sigset_t *set, int signum)
{
	if (signum <= 0 || signum >= NSIG) {
		errno = EINVAL;
		return 0;
	}
	return (*set & ((sigset_t)1 << (signum - 1))) ? 1 : 0;
}
