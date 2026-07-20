#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <setjmp.h>

static _Atomic int caught;
static sigjmp_buf sjb;

static void
usr1_handler(int sig)
{
	(void)sig;
	caught = 7;
}

static void
escape_handler(int sig)
{
	(void)sig;
	caught = 99;
	siglongjmp(sjb, 1);
}

int
main(void)
{
	struct sigaction sa, old;
	sigset_t set, pending, full;

	/* signal() + raise() */
	signal(SIGUSR1, usr1_handler);
	raise(SIGUSR1);
	if (caught != 7) {
		printf("signal/raise failed: caught=%d\n", caught);
		return 1;
	}

	/* sigaction() with SIG_IGN */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sa.sa_restorer = (void (*)(void))0;
	sa.sa_mask = 0;
	if (sigaction(SIGUSR2, &sa, &old) < 0) {
		printf("sigaction set failed\n");
		return 2;
	}
	raise(SIGUSR2);
	if (sigaction(SIGUSR2, &old, (struct sigaction *)0) < 0) {
		printf("sigaction restore failed\n");
		return 3;
	}

	/* sigprocmask + sigpending */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, (sigset_t *)0) < 0) {
		printf("sigprocmask failed\n");
		return 4;
	}
	signal(SIGUSR1, SIG_IGN);
	raise(SIGUSR1);
	sigemptyset(&pending);
	sigpending(&pending);
	if (!sigismember(&pending, SIGUSR1)) {
		printf("sigpending did not see blocked signal\n");
		return 5;
	}
	sigprocmask(SIG_UNBLOCK, &set, (sigset_t *)0);

	/* sigsetjmp/siglongjmp escape from a signal handler */
	caught = 0;
	signal(SIGUSR1, escape_handler);
	if (sigsetjmp(sjb, 1) == 0) {
		raise(SIGUSR1);
		printf("siglongjmp did not happen\n");
		return 6;
	}
	if (caught != 99) {
		printf("sigsetjmp/siglongjmp failed: caught=%d\n", caught);
		return 7;
	}

	/* sigset primitives */
	sigfillset(&full);
	if (!sigismember(&full, SIGTERM) || !sigismember(&full, SIGINT)) {
		printf("sigfillset/sigismember failed\n");
		return 8;
	}
	sigdelset(&full, SIGTERM);
	if (sigismember(&full, SIGTERM)) {
		printf("sigdelset failed\n");
		return 9;
	}

	puts("signal ok");
	return 0;
}
