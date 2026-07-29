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
#if defined(__i386__)
typedef int64_t time_t;
#else
typedef long time_t;
#endif
typedef int clockid_t;
typedef unsigned short sa_family_t;
typedef unsigned int socklen_t;

#endif
