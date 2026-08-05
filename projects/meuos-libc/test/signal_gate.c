/* signal_gate.c — signal()/sigaction()/raise()/kill()/sigprocmask()/
 * sigpending() fine-grained gate.
 *
 * Exercises signal delivery (raise+kill -> handler), per-signal handler
 * installation via sigaction, and the mask/pending machinery: blocking a
 * signal makes raise() leave it pending (visible via sigpending), and
 * unblocking lets the pending handler run.  Complements the coarse signal.c
 * with per-assert signal semantics. */
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static int got_usr1;   /* set by handler; read across raise/sigpending */

static void
on_usr1(int sig)
{
	(void)sig;
	got_usr1 = 1;
}

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d)\n", lbl, errno);
		fails++;
	}
}

int
main(void)
{
	struct sigaction sa, old;
	sigset_t set, pend;

	/* install a SIGUSR1 handler via sigaction and get old disposition */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_usr1;
	sigemptyset(&sa.sa_mask);
	chk("sigaction set", sigaction(SIGUSR1, &sa, &old) == 0);

	/* raise() delivers to self */
	got_usr1 = 0;
	raise(SIGUSR1);
	chk("raise delivers", got_usr1);

	/* kill(getpid(), SIGUSR1) also delivers */
	got_usr1 = 0;
	kill(getpid(), SIGUSR1);
	chk("kill self delivers", got_usr1);

	/* signal() legacy form also installs a handler */
	signal(SIGUSR1, on_usr1);
	got_usr1 = 0;
	raise(SIGUSR1);
	chk("signal legacy delivers", got_usr1);

	/* block SIGUSR1: raise leaves it pending (never delivered yet) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	chk("sigprocmask BLOCK", sigprocmask(SIG_BLOCK, &set, NULL) == 0);
	got_usr1 = 0;
	raise(SIGUSR1);
	chk("blocked after raise", got_usr1 == 0);      /* still masked */
	chk("sigpending", sigpending(&pend) == 0);
	chk("SIGUSR1 pending", sigismember(&pend, SIGUSR1) == 1);

	/* unblock → the pending signal is delivered: it leaves the pending set
	 * and the handler runs (Linux delivers on the unmask boundary). */
	chk("sigprocmask UNBLOCK", sigprocmask(SIG_UNBLOCK, &set, NULL) == 0);
	{
		sigset_t np;
		int i;
		/* spin until the pending bit clears (delivery) or give up */
		for (i = 0; i < 100000; i++) {
			sigpending(&np);
			if (!sigismember(&np, SIGUSR1))
				break;
		}
		chk("pending cleared after unblock", !sigismember(&np, SIGUSR1));
		/* handler is expected to have run */
		chk("unblocked handler ran", got_usr1 == 1);
	}

	/* restore original disposition to stay clean */
	sigaction(SIGUSR1, &old, NULL);

	if (fails) {
		printf("%d signal FAIL\n", fails);
		return 1;
	}
	printf("PASS signal\n");
	return 0;
}
