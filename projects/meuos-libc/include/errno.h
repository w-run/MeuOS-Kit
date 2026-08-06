#ifndef MEUOS_ERRNO_H
#define MEUOS_ERRNO_H

#include <features.h>

__BEGIN_DECLS
int *__errno_location(void);

/* GNU/glibc-convention program-invocation globals.  Populated by the
 * libc startup (startup.c) from argv[0] before main().  Declared here
 * (glibc convention — they live in <errno.h>) so that programs compiling
 * against this sysroot can reference them without opting into the compat
 * layer. */
extern char *program_invocation_name;
extern char *program_invocation_short_name;
__END_DECLS
#define errno (*__errno_location())

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 38
#define ELOOP 40
#define ENAMETOOLONG 36
#define ENOTEMPTY 39
#define ETIMEDOUT 110
#define EALREADY 114
#define ECANCELED 125

#endif
