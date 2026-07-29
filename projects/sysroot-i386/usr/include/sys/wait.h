#ifndef MEUOS_SYS_WAIT_H
#define MEUOS_SYS_WAIT_H

#include <sys/types.h>

pid_t wait(int *);
pid_t waitpid(pid_t, int *, int);

#define WNOHANG 1
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)

#endif
pid_t wait4(pid_t, int *, int, void *);
