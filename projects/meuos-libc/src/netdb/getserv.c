/* netdb/getserv.c — /etc/services parsing for service name/port resolution */

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/* Portable htons (meuos-libc arpa/inet.h doesn't define byte-order helpers) */
static inline unsigned short net_hs(unsigned short s) {
	return (unsigned short)(((s & 0xff00) >> 8) | ((s & 0x00ff) << 8));
}
#define htons(s) net_hs(s)

#define SERVICES_PATH "/etc/services"
#define LINE_MAX 1024

static FILE *servent_file;
static int servent_stayopen;

static _Thread_local struct servent servent_buf;
static _Thread_local char servent_buf_data[1024];

/* Tokenize a line of /etc/services into parts.
 * Returns the number of tokens parsed. */
static int
parse_servline(char *line, char **name, char **port_proto, char **aliases, int max_aliases)
{
	char *p = line;
	int n = 0;

	/* Skip leading whitespace */
	while (*p && (*p == ' ' || *p == '\t')) p++;
	if (!*p || *p == '#')
		return 0;

	/* Service name */
	*name = p;
	while (*p && !isspace((unsigned char)*p)) p++;
	if (*p) *p++ = '\0';

	/* Port/protocol */
	while (*p && isspace((unsigned char)*p)) p++;
	if (!*p) return 0;
	*port_proto = p;
	while (*p && !isspace((unsigned char)*p)) p++;
	if (*p) *p++ = '\0';

	/* Aliases */
	while (*p && n < max_aliases) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p || *p == '#') break;
		aliases[n++] = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		if (*p) *p++ = '\0';
	}
	aliases[n] = NULL;
	return 1;
}

static int
fill_servent(struct servent *se, char *buf, size_t buflen,
             const char *name, const char *port_proto,
             char **aliases)
{
	char *port_s, *proto_s;
	char *p = buf;
	size_t left = buflen;
	size_t len;
	int alias_count;

	/* Split port/protocol */
	port_s = strdup(port_proto); /* temp copy for strtok */
	if (!port_s) return -1;
	proto_s = strchr(port_s, '/');
	if (proto_s) *proto_s++ = '\0';

	/* Validate port */
	char *end;
	long port_val = strtol(port_s, &end, 10);
	if (*end || port_val < 0 || port_val > 65535) {
		free(port_s);
		return -1;
	}
	free(port_s);

	/* Write service name */
	len = strlen(name) + 1;
	if (len > left) return -1;
	memcpy(p, name, len);
	se->s_name = p;
	p += len;
	left -= len;

	/* Write protocol name */
	if (!proto_s) proto_s = "";
	len = strlen(proto_s) + 1;
	if (len > left) return -1;
	memcpy(p, proto_s, len);
	se->s_proto = p;
	p += len;
	left -= len;

	/* Count aliases */
	for (alias_count = 0; aliases[alias_count]; alias_count++);

	/* Write aliases array (pointers + strings) */
	size_t ptrs_size = (alias_count + 1) * sizeof(char *);
	if (ptrs_size > left) return -1;
	se->s_aliases = (char **)p;
	p += ptrs_size;
	left -= ptrs_size;

	for (int i = 0; i < alias_count; i++) {
		len = strlen(aliases[i]) + 1;
		if (len > left) return -1;
		memcpy(p, aliases[i], len);
		se->s_aliases[i] = p;
		p += len;
		left -= len;
	}
	se->s_aliases[alias_count] = NULL;

	se->s_port = htons((unsigned short)port_val);
	return 0;
}

int
getservbyname_r(const char *name, const char *proto,
                struct servent *result, char *buf, size_t buflen,
                struct servent **res)
{
	FILE *f;
	char line[LINE_MAX];
	char *svc_name, *svc_port_proto;
	char *aliases[32];

	if (!name) { *res = NULL; return EINVAL; }

	f = fopen(SERVICES_PATH, "r");
	if (!f) { *res = NULL; return errno; }

	while (fgets(line, sizeof(line), f)) {
		if (!parse_servline(line, &svc_name, &svc_port_proto, aliases, 31))
			continue;
		if (strcmp(svc_name, name) != 0)
			continue;
		if (proto) {
			char *ps = strchr(svc_port_proto, '/');
			char *pp = ps ? ps + 1 : "";
			if (strcasecmp(pp, proto) != 0)
				continue;
		}
		fclose(f);
		if (fill_servent(result, buf, buflen, svc_name, svc_port_proto, aliases) != 0) {
			*res = NULL;
			return ERANGE;
		}
		*res = result;
		return 0;
	}
	fclose(f);
	*res = NULL;
	return 0;
}

int
getservbyport_r(int port, const char *proto,
                struct servent *result, char *buf, size_t buflen,
                struct servent **res)
{
	FILE *f;
	char line[LINE_MAX];
	char *svc_name, *svc_port_proto;
	char *aliases[32];
	unsigned short wanted = (unsigned short)port;

	f = fopen(SERVICES_PATH, "r");
	if (!f) { *res = NULL; return errno; }

	while (fgets(line, sizeof(line), f)) {
		if (!parse_servline(line, &svc_name, &svc_port_proto, aliases, 31))
			continue;
		/* Parse port */
		char *ps = strchr(svc_port_proto, '/');
		size_t plen = ps ? (size_t)(ps - svc_port_proto) : strlen(svc_port_proto);
		char port_str[16];
		if (plen > 15) continue;
		memcpy(port_str, svc_port_proto, plen);
		port_str[plen] = '\0';
		char *end;
		long val = strtol(port_str, &end, 10);
		if (*end || val < 0 || val > 65535) continue;
		if (htons((unsigned short)val) != wanted) continue;
		if (proto) {
			char *pp = ps ? ps + 1 : "";
			if (strcasecmp(pp, proto) != 0)
				continue;
		}
		fclose(f);
		if (fill_servent(result, buf, buflen, svc_name, svc_port_proto, aliases) != 0) {
			*res = NULL;
			return ERANGE;
		}
		*res = result;
		return 0;
	}
	fclose(f);
	*res = NULL;
	return 0;
}

struct servent *
getservbyname(const char *name, const char *proto)
{
	struct servent *res;
	int ret = getservbyname_r(name, proto, &servent_buf,
	                          servent_buf_data, sizeof(servent_buf_data),
	                          &res);
	if (ret != 0 || !res) {
		errno = ret;
		return NULL;
	}
	return res;
}

struct servent *
getservbyport(int port, const char *proto)
{
	struct servent *res;
	int ret = getservbyport_r(port, proto, &servent_buf,
	                          servent_buf_data, sizeof(servent_buf_data),
	                          &res);
	if (ret != 0 || !res) {
		errno = ret;
		return NULL;
	}
	return res;
}

void
setservent(int stayopen)
{
	if (servent_file) rewind(servent_file);
	else if (stayopen) servent_file = fopen(SERVICES_PATH, "r");
	servent_stayopen = stayopen;
}

void
endservent(void)
{
	if (servent_file) {
		fclose(servent_file);
		servent_file = NULL;
	}
}

struct servent *
getservent(void)
{
	char line[LINE_MAX];
	char *svc_name, *svc_port_proto;
	char *aliases[32];

	if (!servent_file) {
		servent_file = fopen(SERVICES_PATH, "r");
		if (!servent_file) return NULL;
	}

	while (fgets(line, sizeof(line), servent_file)) {
		if (!parse_servline(line, &svc_name, &svc_port_proto, aliases, 31))
			continue;
		if (fill_servent(&servent_buf, servent_buf_data,
		                 sizeof(servent_buf_data),
		                 svc_name, svc_port_proto, aliases) != 0)
			continue;
		return &servent_buf;
	}
	return NULL;
}
