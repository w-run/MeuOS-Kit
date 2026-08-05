#ifndef MEUOS_FCNTL_H
#define MEUOS_FCNTL_H

#include <features.h>
#include <sys/types.h>
#include <unistd.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_CLOEXEC 02000000
#define O_DIRECTORY 0200000
#define O_NONBLOCK 04000

__BEGIN_DECLS
int open(const char *, int, ...);
int openat(int, const char *, int, ...);
__END_DECLS
#endif
__BEGIN_DECLS
int fcntl(int, int, ...);
__END_DECLS
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_DUPFD 0
#define FD_CLOEXEC 1
