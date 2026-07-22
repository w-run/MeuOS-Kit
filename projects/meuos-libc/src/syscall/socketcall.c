/* Socket family syscalls.
 *
 * On x86_64 and aarch64 each socket call is a dedicated syscall. On i386
 * Linux multiplexes the whole family through socketcall(102): the second
 * argument is a pointer to a 6-long array the kernel reads the call's
 * arguments from. The subcall numbers below (SC_*) are the stable i386
 * socketcall sub-ids.
 *
 * The non-i386 path uses the x86_64 stable internal syscall numbers
 * (translated to native numbers by __syscall_number for aarch64). */

#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "../internal/syscall.h"

#if defined(__i386__)
/* i386 subcall ids for socketcall(102). */
#define SC_SOCKET      1
#define SC_BIND        2
#define SC_CONNECT     3
#define SC_LISTEN      4
#define SC_ACCEPT      5
#define SC_GETSOCKNAME 6
#define SC_GETPEERNAME 7
#define SC_SOCKETPAIR  8
#define SC_SEND        9
#define SC_RECV        10
#define SC_SENDTO      11
#define SC_RECVFROM    12
#define SC_SHUTDOWN    13
#define SC_SETSOCKOPT  14
#define SC_GETSOCKOPT  15
#define SC_ACCEPT4     18

/* Build the kernel args array and issue socketcall(102, sub, args). */
static long
sc(long sub, long a0, long a1, long a2, long a3, long a4, long a5)
{
	long a[6] = { a0, a1, a2, a3, a4, a5 };
	return __meuos_syscall6(102, sub, (long)a, 0, 0, 0, 0);
}
#endif

int
socket(int domain, int type, int protocol)
{
#if defined(__i386__)
	long v = sc(SC_SOCKET, domain, type, protocol, 0, 0, 0);
#else
	long v = __syscall3(41, domain, type, protocol);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (int)v;
}

int
socketpair(int domain, int type, int protocol, int sv[2])
{
#if defined(__i386__)
	long v = sc(SC_SOCKETPAIR, domain, type, protocol, (long)sv, 0, 0);
#else
	long v = __syscall4(53, domain, type, protocol, (long)sv);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (int)v;
}

int
bind(int descriptor, const struct sockaddr *address, socklen_t length)
{
#if defined(__i386__)
	long v = sc(SC_BIND, descriptor, (long)address, length, 0, 0, 0);
#else
	long v = __syscall3(49, descriptor, (long)address, length);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
connect(int descriptor, const struct sockaddr *address, socklen_t length)
{
#if defined(__i386__)
	long v = sc(SC_CONNECT, descriptor, (long)address, length, 0, 0, 0);
#else
	long v = __syscall3(42, descriptor, (long)address, length);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
listen(int descriptor, int backlog)
{
#if defined(__i386__)
	long v = sc(SC_LISTEN, descriptor, backlog, 0, 0, 0, 0);
#else
	long v = __syscall2(50, descriptor, backlog);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
accept(int descriptor, struct sockaddr *address, socklen_t *length)
{
#if defined(__i386__)
	long v = sc(SC_ACCEPT, descriptor, (long)address, (long)length, 0, 0, 0);
#else
	long v = __syscall3(43, descriptor, (long)address, (long)length);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (int)v;
}

int
accept4(int descriptor, struct sockaddr *address, socklen_t *length, int flags)
{
#if defined(__i386__)
	long v = sc(SC_ACCEPT4, descriptor, (long)address, (long)length, flags, 0, 0);
#else
	long v = __syscall4(288, descriptor, (long)address, (long)length, flags);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (int)v;
}

int
getsockname(int descriptor, struct sockaddr *address, socklen_t *length)
{
#if defined(__i386__)
	long v = sc(SC_GETSOCKNAME, descriptor, (long)address, (long)length, 0, 0, 0);
#else
	long v = __syscall3(51, descriptor, (long)address, (long)length);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
getpeername(int descriptor, struct sockaddr *address, socklen_t *length)
{
#if defined(__i386__)
	long v = sc(SC_GETPEERNAME, descriptor, (long)address, (long)length, 0, 0, 0);
#else
	long v = __syscall3(52, descriptor, (long)address, (long)length);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

ssize_t
send(int descriptor, const void *buffer, size_t length, int flags)
{
#if defined(__i386__)
	long v = sc(SC_SEND, descriptor, (long)buffer, length, flags, 0, 0);
#else
	long v = __syscall4(44, descriptor, (long)buffer, length, flags);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (ssize_t)v;
}

ssize_t
recv(int descriptor, void *buffer, size_t length, int flags)
{
#if defined(__i386__)
	long v = sc(SC_RECV, descriptor, (long)buffer, length, flags, 0, 0);
#else
	long v = __syscall4(45, descriptor, (long)buffer, length, flags);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (ssize_t)v;
}

ssize_t
sendto(int descriptor, const void *buffer, size_t length, int flags,
       const struct sockaddr *address, socklen_t addrlen)
{
#if defined(__i386__)
	long v = sc(SC_SENDTO, descriptor, (long)buffer, length, flags,
	            (long)address, addrlen);
#else
	long v = __meuos_syscall6(__syscall_number(44), descriptor, (long)buffer,
	                         length, flags, (long)address, addrlen);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (ssize_t)v;
}

ssize_t
recvfrom(int descriptor, void *buffer, size_t length, int flags,
         struct sockaddr *address, socklen_t *addrlen)
{
#if defined(__i386__)
	long v = sc(SC_RECVFROM, descriptor, (long)buffer, length, flags,
	            (long)address, (long)addrlen);
#else
	long v = __meuos_syscall6(__syscall_number(45), descriptor, (long)buffer,
	                         length, flags, (long)address, (long)addrlen);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return (ssize_t)v;
}

int
shutdown(int descriptor, int how)
{
#if defined(__i386__)
	long v = sc(SC_SHUTDOWN, descriptor, how, 0, 0, 0, 0);
#else
	long v = __syscall2(48, descriptor, how);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
setsockopt(int descriptor, int level, int optname,
           const void *optval, socklen_t optlen)
{
#if defined(__i386__)
	long v = sc(SC_SETSOCKOPT, descriptor, level, optname,
	            (long)optval, optlen, 0);
#else
	long v = __meuos_syscall6(__syscall_number(54), descriptor, level, optname,
	                         (long)optval, optlen, 0);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
getsockopt(int descriptor, int level, int optname,
           void *optval, socklen_t *optlen)
{
#if defined(__i386__)
	long v = sc(SC_GETSOCKOPT, descriptor, level, optname,
	            (long)optval, (long)optlen, 0);
#else
	long v = __meuos_syscall6(__syscall_number(55), descriptor, level, optname,
	                         (long)optval, (long)optlen, 0);
#endif
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}
