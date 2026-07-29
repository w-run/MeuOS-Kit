#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Portable byteswap helpers (meuos-libc lacks <arpa/inet.h> byte-order funcs) */
static inline unsigned short
net_hs(unsigned short s)
{
	return (unsigned short)(((s & 0xff00) >> 8) | ((s & 0x00ff) << 8));
}
static inline unsigned int
net_hl(unsigned int l)
{
	return ((l & 0xff000000) >> 24) | ((l & 0x00ff0000) >> 8) |
	       ((l & 0x0000ff00) << 8)  | ((l & 0x000000ff) << 24);
}
#define htons(s) net_hs(s)
#define htonl(l) net_hl(l)
#define ntohs(s) net_hs(s)

/* in6addr_any and in6addr_loopback (netinet/in.h doesn't define them yet) */
static const struct in6_addr in6addr_any_local; /* zero-initialized = :: */
static const struct in6_addr in6addr_loopback_local = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}};
#define in6addr_any in6addr_any_local
#define in6addr_loopback in6addr_loopback_local

const char *
gai_strerror(int ecode)
{
	switch (ecode) {
	case 0:               return "Success";
	case EAI_BADFLAGS:    return "Invalid flags";
	case EAI_NONAME:      return "Name or service not known";
	case EAI_AGAIN:       return "Temporary name service failure";
	case EAI_FAIL:        return "Non-recoverable name service failure";
	case EAI_NODATA:      return "No address associated with name";
	case EAI_FAMILY:      return "Address family not supported";
	case EAI_SOCKTYPE:    return "Socket type not supported";
	case EAI_SERVICE:     return "Service not supported for socket type";
	case EAI_ADDRFAMILY:  return "Address family not supported";
	case EAI_MEMORY:      return "Memory allocation failure";
	case EAI_SYSTEM:      return "System error";
	case EAI_OVERFLOW:    return "Buffer overflow";
	case EAI_INPROGRESS:  return "Processing in progress";
	case EAI_CANCELED:    return "Cancelled";
	case EAI_NOTCANCELED: return "Not cancelled";
	case EAI_ALLDONE:     return "All done";
	case EAI_INTR:        return "Interrupted";
	case EAI_IDN_ENCODE:  return "IDN encoding error";
	default:              return "Unknown error";
	}
}

void
freeaddrinfo(struct addrinfo *ai)
{
	struct addrinfo *next;
	while (ai) {
		next = ai->ai_next;
		free(ai->ai_canonname);
		free(ai->ai_addr);
		free(ai);
		ai = next;
	}
}

/* Name resolution via numerical IP or gethostbyname_r */
struct hostent *
__resolve_host(const char *name, int family)
{
	static _Thread_local struct hostent he;
	static _Thread_local char buf[1024];
	struct hostent *result = NULL;
	int h_errno_local;

	/* Try numeric first */
	if (name) {
		static _Thread_local struct in_addr addr4;
		static _Thread_local struct in6_addr addr6;
		if (family != AF_INET6 && inet_pton(AF_INET, name, &addr4) == 1) {
			he.h_name = (char *)name;
			he.h_aliases = NULL;
			he.h_addrtype = AF_INET;
			he.h_length = 4;
			he.h_addr_list = (char *[2]){(char *)&addr4, NULL};
			return &he;
		}
		if (family != AF_INET && inet_pton(AF_INET6, name, &addr6) == 1) {
			he.h_name = (char *)name;
			he.h_aliases = NULL;
			he.h_addrtype = AF_INET6;
			he.h_length = 16;
			he.h_addr_list = (char *[2]){(char *)&addr6, NULL};
			return &he;
		}
		/* Try file-based lookup */
		if (gethostbyname_r(name, &he, buf, sizeof buf, &result, &h_errno_local) == 0 && result)
			return result;
	}
	return NULL;
}

int
getaddrinfo(const char *node, const char *service,
            const struct addrinfo *hints, struct addrinfo **res)
{
	int family = AF_UNSPEC;
	int flags = 0;
	int socktype = 0;
	int protocol = 0;
	int port = 0;
	int naddrs = 0;
	struct addrinfo *prev = NULL;
	struct addrinfo *first = NULL;
	int err;
	struct hostent *he;

	if (!node && !service)
		return EAI_NONAME;

	if (hints) {
		family = hints->ai_family;
		flags = hints->ai_flags;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
		              AI_NUMERICSERV | AI_V4MAPPED | AI_ALL |
		              AI_ADDRCONFIG))
			return EAI_BADFLAGS;
		if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
			return EAI_FAMILY;
	}

	/* Resolve service to port number */
	if (service) {
		char *end;
		long n = strtol(service, &end, 10);
		if (*end == '\0' && n >= 0 && n <= 65535) {
			port = htons((unsigned short)n);
		} else if (!(flags & AI_NUMERICSERV)) {
			struct servent *se = getservbyname(service, NULL);
			if (!se)
				return EAI_SERVICE;
			port = se->s_port;
		} else {
			return EAI_NONAME;
		}
	} else if (!node) {
		port = 0;
	}

	/* Resolve node to addresses */
	if (!node) {
		/* No node: use wildcard or loopback if AI_PASSIVE */
		he = NULL;
		naddrs = 1;
	} else if (flags & AI_NUMERICHOST) {
		/* Numeric only: validate address */
		struct in_addr addr4;
		struct in6_addr addr6;
		if ((family != AF_INET6 && inet_pton(AF_INET, node, &addr4) == 1) ||
		    (family != AF_INET && inet_pton(AF_INET6, node, &addr6) == 1)) {
			he = NULL;
			naddrs = 1;
		} else {
			return EAI_NONAME;
		}
	} else {
		he = __resolve_host(node, family);
		if (!he)
			return EAI_NONAME;
		naddrs = (he->h_addr_list ? 1 : 0);
	}

	/* Build addrinfo chain */
	int addr_idx = 0;
	do {
		struct addrinfo *ai = calloc(1, sizeof(struct addrinfo));
		if (!ai) { freeaddrinfo(first); return EAI_MEMORY; }

		ai->ai_family = AF_INET;
		ai->ai_socktype = socktype ? socktype : SOCK_STREAM;
		ai->ai_protocol = protocol ? protocol : (socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP);

		if (he && he->h_addr_list && he->h_addr_list[addr_idx]) {
			ai->ai_family = he->h_addrtype;
			/* Numeric address passed to getaddrinfo or /etc/hosts IP */
		}

		if (ai->ai_family == AF_INET) {
			struct sockaddr_in *sin = calloc(1, sizeof(struct sockaddr_in));
			if (!sin) { freeaddrinfo(first); free(ai); return EAI_MEMORY; }
			sin->sin_family = AF_INET;
			sin->sin_port = port;
			if (he && he->h_addr_list && he->h_addr_list[addr_idx])
				memcpy(&sin->sin_addr, he->h_addr_list[addr_idx], 4);
			else if (!node)
				sin->sin_addr.s_addr = (flags & AI_PASSIVE) ? INADDR_ANY : htonl(0x7f000001);
			else {
				inet_pton(AF_INET, node, &sin->sin_addr);
			}
			ai->ai_addr = (struct sockaddr *)sin;
			ai->ai_addrlen = sizeof(struct sockaddr_in);
		} else if (ai->ai_family == AF_INET6) {
			struct sockaddr_in6 *sin6 = calloc(1, sizeof(struct sockaddr_in6));
			if (!sin6) { freeaddrinfo(first); free(ai); return EAI_MEMORY; }
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = port;
			if (he && he->h_addr_list && he->h_addr_list[addr_idx])
				memcpy(&sin6->sin6_addr, he->h_addr_list[addr_idx], 16);
			else if (!node) {
				memset(&sin6->sin6_addr, 0, 16); /* in6addr_any */
				if (!(flags & AI_PASSIVE))
					sin6->sin6_addr.s6_addr[15] = 1; /* ::1 loopback */
		} else
				inet_pton(AF_INET6, node, &sin6->sin6_addr);
			ai->ai_addr = (struct sockaddr *)sin6;
			ai->ai_addrlen = sizeof(struct sockaddr_in6);
		}

		/* Canonical name */
		if ((flags & AI_CANONNAME) && he && he->h_name) {
			ai->ai_canonname = strdup(he->h_name);
			if (!ai->ai_canonname) { freeaddrinfo(first); return EAI_MEMORY; }
		}

		if (prev)
			prev->ai_next = ai;
		else
			first = ai;
		prev = ai;
		addr_idx++;
	} while (he && he->h_addr_list && he->h_addr_list[addr_idx]);

	*res = first;
	return 0;
}

int
getnameinfo(const struct sockaddr *sa, socklen_t salen,
            char *host, socklen_t hostlen,
            char *serv, socklen_t servlen, int flags)
{
	if (flags & ~(NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN |
	              NI_NAMEREQD | NI_DGRAM | NI_NUMERICSCOPE))
		return EAI_BADFLAGS;

	/* Format host */
	if (host && hostlen > 0) {
		if (flags & NI_NUMERICHOST) {
			const char *result = NULL;
			char buf[INET6_ADDRSTRLEN];
			if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in)) {
				const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
				result = inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
			} else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6)) {
				const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
				result = inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof buf);
			}
			if (!result)
				return EAI_FAMILY;
			strncpy(host, result, hostlen - 1);
			host[hostlen - 1] = '\0';
		} else if (flags & NI_NAMEREQD) {
			return EAI_NONAME;
		} else {
			/* Default to numeric if no reverse lookup available */
			const char *result = NULL;
			char buf[INET6_ADDRSTRLEN];
			if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in)) {
				const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
				result = inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf);
			} else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6)) {
				const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
				result = inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof buf);
			}
			if (result) {
				strncpy(host, result, hostlen - 1);
				host[hostlen - 1] = '\0';
			} else {
				host[0] = '\0';
			}
		}
	}

	/* Format service */
	if (serv && servlen > 0) {
		if (flags & NI_NUMERICSERV) {
			in_port_t p = 0;
			if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in))
				p = ((const struct sockaddr_in *)sa)->sin_port;
			else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6))
				p = ((const struct sockaddr_in6 *)sa)->sin6_port;
			snprintf(serv, servlen, "%u", (unsigned)ntohs(p));
		} else {
			/* Default to numeric */
			in_port_t p = 0;
			if (sa->sa_family == AF_INET && salen >= sizeof(struct sockaddr_in))
				p = ((const struct sockaddr_in *)sa)->sin_port;
			else if (sa->sa_family == AF_INET6 && salen >= sizeof(struct sockaddr_in6))
				p = ((const struct sockaddr_in6 *)sa)->sin6_port;
			snprintf(serv, servlen, "%u", (unsigned)ntohs(p));
		}
	}

	return 0;
}
