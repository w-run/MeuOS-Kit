#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/* Thread-local static buffer for legacy getproto* functions */
static _Thread_local struct protoent protoent_buf;
static _Thread_local char protoent_data[1024];
static FILE *protoent_file;

static int
parse_proto_line(const char *line, struct protoent *pe, char *buf, size_t buflen)
{
	const char *p = line;
	char *name, *number_str, *aliases;
	size_t used = 0;
	int alias_count = 0;
	char **alias_ptr;

	/* Skip leading whitespace */
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
	pe->p_name = name;

	/* Skip whitespace */
	while (*p == ' ' || *p == '\t') p++;

	/* Number */
	number_str = buf + used;
	while (*p && !isspace((unsigned char)*p)) {
		if (used < buflen - 1) buf[used++] = *p;
		p++;
	}
	buf[used] = '\0';
	if (used == 0 || used == (size_t)(number_str - buf)) return -1;
	used++;
	pe->p_proto = atoi(number_str);

	/* Aliases */
	alias_ptr = (char **)(buf + used);
	used += sizeof(char *);
	pe->p_aliases = alias_ptr;
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

struct protoent *
getprotobyname(const char *name)
{
	FILE *f = fopen("/etc/protocols", "r");
	if (!f) return NULL;

	char line[512];
	struct protoent *result = NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_proto_line(line, &protoent_buf, protoent_data, sizeof protoent_data) == 0) {
			if (strcmp(protoent_buf.p_name, name) == 0) {
				result = &protoent_buf;
				break;
			}
		}
	}
	fclose(f);
	return result;
}

struct protoent *
getprotobynumber(int proto)
{
	FILE *f = fopen("/etc/protocols", "r");
	if (!f) return NULL;

	char line[512];
	struct protoent *result = NULL;
	while (fgets(line, sizeof line, f)) {
		if (parse_proto_line(line, &protoent_buf, protoent_data, sizeof protoent_data) == 0) {
			if (protoent_buf.p_proto == proto) {
				result = &protoent_buf;
				break;
			}
		}
	}
	fclose(f);
	return result;
}

void
setprotoent(int stayopen)
{
	if (protoent_file) fclose(protoent_file);
	protoent_file = fopen("/etc/protocols", "r");
	if (!stayopen && protoent_file) {
		fclose(protoent_file);
		protoent_file = NULL;
	}
}

void
endprotoent(void)
{
	if (protoent_file) {
		fclose(protoent_file);
		protoent_file = NULL;
	}
}

struct protoent *
getprotoent(void)
{
	if (!protoent_file) {
		protoent_file = fopen("/etc/protocols", "r");
		if (!protoent_file) return NULL;
	}
	char line[512];
	while (fgets(line, sizeof line, protoent_file)) {
		if (parse_proto_line(line, &protoent_buf, protoent_data, sizeof protoent_data) == 0)
			return &protoent_buf;
	}
	return NULL;
}
