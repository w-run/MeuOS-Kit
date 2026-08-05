#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <sys/types.h>
#include <stdint.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

/* Host <-> network byte order. */
uint32_t htonl(uint32_t);
uint32_t ntohl(uint32_t);
uint16_t htons(uint16_t);
uint16_t ntohs(uint16_t);

struct in_addr {
	in_addr_t s_addr;
};

struct sockaddr_in {
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};

struct in6_addr {
	unsigned char s6_addr[16];
};

struct sockaddr_in6 {
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

/* IPPROTO_* */
#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_IPV6    41
#define IPPROTO_RAW     255

/* SOL_IP */
#define IP_TOS          1
#define IP_TTL          2

/* INADDR_* */
#define INADDR_ANY      ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE     ((in_addr_t)0xffffffff)

#endif
