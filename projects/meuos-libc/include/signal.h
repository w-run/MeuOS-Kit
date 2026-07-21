#ifndef MEUOS_SIGNAL_H
#define MEUOS_SIGNAL_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linux signal numbers (asm-generic, identical on x86_64). */
#define SIGHUP      1
#define SIGINT      2
#define SIGQUIT     3
#define SIGILL      4
#define SIGTRAP     5
#define SIGABRT     6
#define SIGIOT      6
#define SIGBUS      7
#define SIGFPE      8
#define SIGKILL     9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCLD     17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22
#define SIGURG     23
#define SIGXCPU    24
#define SIGXFSZ    25
#define SIGVTALRM  26
#define SIGPROF    27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPOLL    29
#define SIGPWR     30
#define SIGSYS     31
#define SIGUNUSED  31

#define NSIG       32

/* Signal disposition sentinels. */
#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int))-1)

/* sigaction flags. */
#define SA_NOCLDSTOP  0x00000001
#define SA_NOCLDWAIT  0x00000002
#define SA_SIGINFO    0x00000004
#define SA_RESTORER   0x04000000
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#define SA_NOMASK     SA_NODEFER
#define SA_ONESHOT    SA_RESETHAND

/* sigprocmask how values. */
#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

#define MINSIGSTKSZ 2048
#define SIGSTKSZ     8192

typedef int sig_atomic_t;
/* Kernel-size sigset: 64 bits covers all real-time signals on x86_64. */
typedef uint64_t sigset_t;

/* Minimal siginfo_t: enough to expose the SA_SIGINFO third argument shape
 * without dragging in the full kernel layout.  The first three members match
 * the kernel ABI ordering so an SA_SIGINFO handler can read si_signo/si_code
 * portably. */
typedef struct {
    int si_signo;
    int si_errno;
    int si_code;
    int __pad0;
    union {
        int __pad[(128 - 4 * sizeof(int)) / sizeof(int)];
        struct {
            pid_t si_pid;
            uid_t si_uid;
        } __kill;
        struct {
            void *si_addr;
        } __sigfault;
    } __fields;
} siginfo_t;
#define si_pid      __fields.__kill.si_pid
#define si_uid      __fields.__kill.si_uid
#define si_addr     __fields.__sigfault.si_addr

typedef struct {
    void *ss_sp;
    int ss_flags;
    size_t ss_size;
} stack_t;

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    } __sigaction_handler;
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};
#define sa_handler      __sigaction_handler.sa_handler
#define sa_sigaction    __sigaction_handler.sa_sigaction

int raise(int);
void (*signal(int, void (*)(int)))(int);
int kill(pid_t, int);
int tgkill(pid_t, pid_t, int);
int sigaction(int, const struct sigaction *, struct sigaction *);
int sigprocmask(int, const sigset_t *, sigset_t *);
int sigpending(sigset_t *);
int sigsuspend(const sigset_t *);
int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);
int sigaltstack(const stack_t *, stack_t *);

#ifdef __cplusplus
}
#endif

#endif
