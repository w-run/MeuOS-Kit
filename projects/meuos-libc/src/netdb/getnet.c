#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* Portable ntohl (meuos-libc lacks byte-order helpers) */
static inline unsigned int net_nl(unsigned int l) {
	return ((l & 0xff000000) >> 24) | ((l & 0x00ff0000) >> 8) |
	       ((l & 0x0000ff00) << 8)  | ((l & 0x000000ff) << 24);
}
#define ntohl(l) net_nl(l)

/* Thread-local static buffer for legacy getnet* functions */
static _Thread_local struct netent netent_buf;
static _Thread_local char netent_data[1024];
static FILE *netent_file;

static int
parse_net_line(const char *line, struct netent *ne, char *buf, size_t buflen)
{
	const char *p = line;
	char *name, *net_str, *aliases;
	size_t used = 0;
	int alias_count = 0;
	char **alias_ptr;

	while (*p == ' ' || *p == '\t') p++;
	if (!*p || *p == '#') return -1;

	/* Name */
	name = buf + used;
	while (*p && !isspace((unsigned char)*p) && *p != '#') {
		if (used < buflen - 1) buf[used++] = *p;
		p++;
	}
	buf[used] = '\0';
	if (used == 0) return -1;
	used++;
	ne->n_name = name;

	while (*p == ' ' || *p == '\t') p++;

	/* Network number (dotted quad) */
	net_str = buf + used;
	while (*p && !isspace((unsigned char)*p)) {
		if (used < buflen - 1) buf[used++] = *p;
		p++;
	}
	buf[used] = '\0';
	if (used == 0) return -1;
	used++;
	{
		struct in_addr addr;
		if (inet_aton(net_str, &addr))
			ne->n_net = ntohl(addr.s_addr);
		else
			ne->n_net = (uint32_t)strtol(net_str, NULL, 10);
	}
	ne->n_addrtype = AF_INET;

	/* Aliases */
	alias_ptr = (char **)(buf + used);
	used += sizeof(char *);
	ne->n_aliases = alias_ptr;
	alias_count = 0;

	while (*p == ' ' || *p == '\t') p++;
	while (*p && *p != '#') {
		char *alias = buf + used;
		while (*p && !isspace((unsigned char)*p) && *p != '#') {
			if (used < buflen - 1) buf[used++] = *p;
			p++;
		}
		buf[used] = '\0';
		if (used >= buflen - 1) break;
		used++;
		alias_ptr[alias_count++] = alias;
		while (*p == ' ' || *p == '\t') p++;
	}
	alias_ptr[alias_count] = NULL;

	return 0;
}

struct netent *
getnetbyname(const char *name)
{
	FILE *f = fopen("/etc/networks", "r");
	if (!f) return NULL;

	char line[512];
	struct netent *result = NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_net_line(line, &netent_buf, netent_data, sizeof netent_data) == 0) {
			if (strcmp(netent_buf.n_name, name) == 0) {
				result = &netent_buf;
				break;
			}
		}
	}
	fclose(f);
	return result;
}

struct netent *
getnetbyaddr(uint32_t net, int type)
{
	if (type != AF_INET) return NULL;

	FILE *f = fopen("/etc/networks", "r");
	if (!f) return NULL;

	char line[512];
	struct netent *result = NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_net_line(line, &netent_buf, netent_data, sizeof netent_data) == 0) {
			if (netent_buf.n_net == net) {
				result = &netent_buf;
				break;
			}
		}
	}
	fclose(f);
	return result;
}

void
setnetent(int stayopen)
{
	if (netent_file) fclose(netent_file);
	netent_file = fopen("/etc/networks", "r");
	if (!stayopen && netent_file) {
		fclose(netent_file);
		netent_file = NULL;
	}
}

void
endnetent(void)
{
	if (netent_file) {
		fclose(netent_file);
		netent_file = NULL;
	}
}

struct netent *
getnetent(void)
{
	if (!netent_file) {
		netent_file = fopen("/etc/networks", "r");
		if (!netent_file) return NULL;
	}
	char line[512];
	while (fgets(line, sizeof line, netent_file)) {
		if (parse_net_line(line, &netent_buf, netent_data, sizeof netent_data) == 0)
			return &netent_buf;
	}
	return NULL;
}
