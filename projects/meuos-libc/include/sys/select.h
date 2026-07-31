#ifndef MEUOS_SYS_SELECT_H
#define MEUOS_SYS_SELECT_H

/* fd_set 与 FD_* 宏由 <sys/types.h> 提供（glibc 兼容，见该文件注释）。 */
#include <sys/types.h>
#include <time.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);

#ifdef __cplusplus
}
#endif

#endif
