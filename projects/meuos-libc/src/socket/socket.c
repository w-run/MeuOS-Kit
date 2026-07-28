/* socket/socket.c — POSIX socket system call wrappers for Linux */

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include "../internal/syscall.h"

/* Linux socketcall sub-ids (for i386 compatibility; x86_64 uses direct
 * syscalls but socketcall works for both via __syscall). */
#define SYS_SOCKET     1
#define SYS_BIND       2
#define SYS_CONNECT    3
#define SYS_LISTEN     4
#define SYS_ACCEPT     5
#define SYS_GETSOCKNAME 6
#define SYS_GETPEERNAME 7
#define SYS_SOCKETPAIR 8
#define SYS_SEND       9
#define SYS_RECV      10
#define SYS_SENDTO    11
#define SYS_RECVFROM  12
#define SYS_SHUTDOWN  13
#define SYS_SETSOCKOPT 14
#define SYS_GETSOCKOPT 15
#define SYS_SENDMSG   16
#define SYS_RECVMSG   17
#define SYS_ACCEPT4   18

/* x86_64 has individual syscalls; use SYS_socketcall on i386 where only
 * socketcall(2) is available.  On x86_64, we use direct __syscall. */
#if defined(__x86_64__)
#  define LINUX_SYS_SOCKET 41
#  define LINUX_SYS_CONNECT 42
#  define LINUX_SYS_ACCEPT 43
#  define LINUX_SYS_SENDTO 44
#  define LINUX_SYS_RECVFROM 45
#  define LINUX_SYS_SENDMSG 46
#  define LINUX_SYS_RECVMSG 47
#  define LINUX_SYS_SHUTDOWN 48
#  define LINUX_SYS_BIND 49
#  define LINUX_SYS_LISTEN 50
#  define LINUX_SYS_GETSOCKNAME 51
#  define LINUX_SYS_GETPEERNAME 52
#  define LINUX_SYS_SOCKETPAIR 53
#  define LINUX_SYS_SETSOCKOPT 54
#  define LINUX_SYS_GETSOCKOPT 55
#  define LINUX_SYS_ACCEPT4 288
#else
/* i386/others: use socketcall(102) multiplexer */
extern long __socketcall(long call, unsigned long *args);
#  define SYS_socketcall 102
#endif

int
socket(int domain, int type, int protocol)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_SOCKET, domain, type, protocol);
#else
	unsigned long args[3] = { (unsigned long)domain, (unsigned long)type, (unsigned long)protocol };
	ret = __socketcall(SYS_SOCKET, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_BIND, sockfd, (long)addr, addrlen);
#else
	unsigned long args[3] = { (unsigned long)sockfd, (unsigned long)addr, addrlen };
	ret = __socketcall(SYS_BIND, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
listen(int sockfd, int backlog)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall2(LINUX_SYS_LISTEN, sockfd, backlog);
#else
	unsigned long args[2] = { (unsigned long)sockfd, (unsigned long)backlog };
	ret = __socketcall(SYS_LISTEN, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_ACCEPT, sockfd, (long)addr, (long)addrlen);
#else
	unsigned long args[3] = { (unsigned long)sockfd, (unsigned long)addr, (unsigned long)addrlen };
	ret = __socketcall(SYS_ACCEPT, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_CONNECT, sockfd, (long)addr, addrlen);
#else
	unsigned long args[3] = { (unsigned long)sockfd, (unsigned long)addr, addrlen };
	ret = __socketcall(SYS_CONNECT, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

ssize_t
sendto(int sockfd, const void *buf, size_t len, int flags,
       const struct sockaddr *dest_addr, socklen_t addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall6(LINUX_SYS_SENDTO, sockfd, (long)buf, len, flags, (long)dest_addr, addrlen);
#else
	unsigned long args[6] = { (unsigned long)sockfd, (unsigned long)buf, len, (unsigned long)flags, (unsigned long)dest_addr, addrlen };
	ret = __socketcall(SYS_SENDTO, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t
recvfrom(int sockfd, void *buf, size_t len, int flags,
         struct sockaddr *src_addr, socklen_t *addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall6(LINUX_SYS_RECVFROM, sockfd, (long)buf, len, flags, (long)src_addr, (long)addrlen);
#else
	unsigned long args[6] = { (unsigned long)sockfd, (unsigned long)buf, len, (unsigned long)flags, (unsigned long)src_addr, (unsigned long)addrlen };
	ret = __socketcall(SYS_RECVFROM, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t
send(int sockfd, const void *buf, size_t len, int flags)
{
	return sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t
recv(int sockfd, void *buf, size_t len, int flags)
{
	return recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

int
shutdown(int sockfd, int how)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall2(LINUX_SYS_SHUTDOWN, sockfd, how);
#else
	unsigned long args[2] = { (unsigned long)sockfd, (unsigned long)how };
	ret = __socketcall(SYS_SHUTDOWN, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
setsockopt(int sockfd, int level, int optname,
           const void *optval, socklen_t optlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall6(LINUX_SYS_SETSOCKOPT, sockfd, level, optname, (long)optval, optlen, 0);
#else
	unsigned long args[5] = { (unsigned long)sockfd, (unsigned long)level, (unsigned long)optname, (unsigned long)optval, optlen };
	ret = __socketcall(SYS_SETSOCKOPT, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
getsockopt(int sockfd, int level, int optname,
           void *optval, socklen_t *optlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall6(LINUX_SYS_GETSOCKOPT, sockfd, level, optname, (long)optval, (long)optlen, 0);
#else
	unsigned long args[5] = { (unsigned long)sockfd, (unsigned long)level, (unsigned long)optname, (unsigned long)optval, (unsigned long)optlen };
	ret = __socketcall(SYS_GETSOCKOPT, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_GETSOCKNAME, sockfd, (long)addr, (long)addrlen);
#else
	unsigned long args[3] = { (unsigned long)sockfd, (unsigned long)addr, (unsigned long)addrlen };
	ret = __socketcall(SYS_GETSOCKNAME, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall3(LINUX_SYS_GETPEERNAME, sockfd, (long)addr, (long)addrlen);
#else
	unsigned long args[3] = { (unsigned long)sockfd, (unsigned long)addr, (unsigned long)addrlen };
	ret = __socketcall(SYS_GETPEERNAME, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
socketpair(int domain, int type, int protocol, int sv[2])
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall4(LINUX_SYS_SOCKETPAIR, domain, type, protocol, (long)sv);
#else
	unsigned long args[4] = { (unsigned long)domain, (unsigned long)type, (unsigned long)protocol, (unsigned long)sv };
	ret = __socketcall(SYS_SOCKETPAIR, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}

int
accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	long ret;
#if defined(__x86_64__)
	ret = __syscall4(LINUX_SYS_ACCEPT4, sockfd, (long)addr, (long)addrlen, flags);
#else
	unsigned long args[4] = { (unsigned long)sockfd, (unsigned long)addr, (unsigned long)addrlen, (unsigned long)flags };
	ret = __socketcall(SYS_ACCEPT4, args);
#endif
	if (__syscall_error(ret)) { errno = (int)-ret; return -1; }
	return (int)ret;
}
