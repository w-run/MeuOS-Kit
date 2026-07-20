#ifndef MEUOS_SYS_SOCKET_H
#define MEUOS_SYS_SOCKET_H

#include <sys/types.h>

#define AF_UNIX 1
#define AF_LOCAL AF_UNIX
#define SOCK_STREAM 1

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);

#endif
