#ifndef MEUOS_SYS_SOCKET_H
#define MEUOS_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned int socklen_t;

/* Address families. */
#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_LOCAL  AF_UNIX
#define AF_INET   2
#define AF_INET6  10

/* Socket types (Linux values; SOCK_*, low bits). */
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3
#define SOCK_SEQPACKET 5
#define SOCK_NONBLOCK  0x800
#define SOCK_CLOEXEC   0x80000

/* Message flags. */
#define MSG_OOB       0x1
#define MSG_PEEK      0x2
#define MSG_DONTROUTE 0x4
#define MSG_TRUNC     0x20
#define MSG_DONTWAIT  0x40
#define MSG_WAITALL   0x100
#define MSG_NOSIGNAL  0x4000

/* Socket-level options. */
#define SOL_SOCKET 1
#define SO_DEBUG   1
#define SO_REUSEADDR 2
#define SO_TYPE    3
#define SO_ERROR   4
#define SO_BROADCAST 6
#define SO_SNDBUF  7
#define SO_RCVBUF  8
#define SO_KEEPALIVE 9
#define SO_LINGER  13

/* shutdown() `how` values. */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

int socket(int, int, int);
int socketpair(int, int, int, int[2]);
int bind(int, const struct sockaddr *, socklen_t);
int connect(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int accept4(int, struct sockaddr *, socklen_t *, int);
int getsockname(int, struct sockaddr *, socklen_t *);
int getpeername(int, struct sockaddr *, socklen_t *);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
int shutdown(int, int);
int setsockopt(int, int, int, const void *, socklen_t);
int getsockopt(int, int, int, void *, socklen_t *);

#endif
