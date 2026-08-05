#ifndef MEUOS_SYS_TYPES_H
#define MEUOS_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef intptr_t ssize_t;
typedef long off_t;
typedef int32_t suseconds_t;
typedef int32_t blksize_t;
typedef int64_t blkcnt_t;
typedef uint32_t mode_t;
typedef uint32_t dev_t;
typedef uint64_t ino_t;
typedef unsigned long nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef long pid_t;
typedef unsigned int id_t;
#if defined(__i386__)
typedef int64_t time_t;
#else
typedef long time_t;
#endif
typedef int clockid_t;
typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

/* fd_set / FD_* 放在这里是为了 glibc 兼容：glibc 的 <sys/types.h> 直接提供
 * fd_set 与 FD_* 宏，不少应用（如 meow）只包含 <sys/types.h> 就使用它们。
 * <sys/select.h> 复用本定义，仅追加 select()/pselect() 声明。 */
#define FD_SETSIZE 1024

typedef struct {
	unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b = (s)->fds_bits; for (__i = sizeof(fd_set) / sizeof(unsigned long); __i; __i--) *__b++ = 0; } while (0)
#define FD_SET(d, s)   ((s)->fds_bits[(d) / (8 * sizeof(unsigned long))] |= (1UL << ((d) % (8 * sizeof(unsigned long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d) / (8 * sizeof(unsigned long))] &= ~(1UL << ((d) % (8 * sizeof(unsigned long)))))
#define FD_ISSET(d, s) (!!((s)->fds_bits[(d) / (8 * sizeof(unsigned long))] & (1UL << ((d) % (8 * sizeof(unsigned long))))))

#endif
