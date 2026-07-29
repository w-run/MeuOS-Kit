#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

/* Maximum number of addresses and aliases per hostent */
#define MAX_ADDRS  8
#define MAX_ALIASES 16

/* Internal buffer for the non-reentrant legacy functions.
 * Thread-local to provide basic thread safety. */
static _Thread_local struct hostent hostent_buf;
static _Thread_local char *hostent_data;
static _Thread_local size_t hostent_datalen;
static _Thread_local int hostent_initialized;

/* Reallocate or allocate the thread-local buffer.
 * Returns 0 on success, -1 on failure (h_errno set). */
static int
ensure_tls_buf(size_t needed)
{
	if (hostent_initialized && hostent_datalen >= needed)
		return 0;
	free(hostent_data);
	hostent_data = malloc(needed);
	if (!hostent_data) {
		hostent_datalen = 0;
		hostent_initialized = 0;
		*__h_errno_location() = NO_RECOVERY;
		return -1;
	}
	hostent_datalen = needed;
	hostent_initialized = 1;
	return 0;
}

/* Parse a line from /etc/hosts.
 * Format: IP canonical_name [alias1 alias2 ...]
 * Returns 1 if line matches name (string), 0 otherwise.
 * When match_name is NULL, match any line (for gethostbyaddr). */
static int
match_hosts_line(const char *line, const char *match_addr,
                 const char *match_name, struct in_addr *out_addr,
                 char *out_canon, size_t canonical_size)
{
	const char *p = line;
	char ip_str[64];
	char canon[256];
	char *aliases[MAX_ALIASES];
	int alias_count = 0;
	int i;

	/* Skip blanks */
	while (*p == ' ' || *p == '\t') p++;

	/* Skip comment lines and blank lines */
	if (*p == '#' || *p == '\n' || *p == '\0')
		return 0;

	/* Extract IP address */
	i = 0;
	while (*p && !isspace((unsigned char)*p) && i < (int)sizeof(ip_str) - 1)
		ip_str[i++] = *p++;
	ip_str[i] = '\0';

	/* Parse IP to check validity */
	if (inet_pton(AF_INET, ip_str, out_addr) != 1)
		return 0;  /* Not a valid IPv4 address */

	/* If matching by address, check now */
	if (match_addr && strcmp(ip_str, match_addr) != 0)
		return 0;

	/* Skip spaces after IP */
	while (*p == ' ' || *p == '\t') p++;

	/* Extract canonical name */
	if (*p == '#' || *p == '\n' || *p == '\0')
		return 0;  /* No name on this line */

	i = 0;
	while (*p && !isspace((unsigned char)*p) && i < 255)
		canon[i++] = *p++;
	canon[i] = '\0';

	/* If matching by name, check now */
	if (match_name && strcasecmp(canon, match_name) != 0) {
		/* Also check aliases */
		int found = 0;
		while (*p == ' ' || *p == '\t') p++;
		while (*p && *p != '#' && *p != '\n') {
			char alias_buf[256];
			int j = 0;
			while (*p && !isspace((unsigned char)*p) && *p != '#' && j < 255)
				alias_buf[j++] = *p++;
			alias_buf[j] = '\0';
			if (strcasecmp(alias_buf, match_name) == 0) {
				found = 1;
				break;
			}
			while (*p == ' ' || *p == '\t') p++;
		}
		if (!found)
			return 0;
	}

	/* Copy canonical name to output */
	if (out_canon && canonical_size)
		strncpy(out_canon, canon, canonical_size);

	return 1;
}

/* Look up a name in /etc/hosts.
 * Returns the number of addresses found (0 = not found),
 * or -1 on error (with h_errno set). */
static int
lookup_name_hosts(const char *name, int family,
                  struct in_addr *addrs, int max_addrs,
                  char *canon, size_t canonical_size)
{
	FILE *f;
	char line[1024];
	int count = 0;
	int found_canon = 0;

	if (family != AF_INET && family != AF_UNSPEC)
		return 0;

	f = fopen("/etc/hosts", "r");
	if (!f) {
		if (errno == ENOENT || errno == ENOTDIR || errno == EACCES)
			return 0;
		*__h_errno_location() = NO_RECOVERY;
		return -1;
	}

	while (fgets(line, sizeof(line), f) && count < max_addrs) {
		struct in_addr addr;
		char line_canon[256];
		int matched;

		matched = match_hosts_line(line, NULL, name,
		                           &addr, line_canon, sizeof(line_canon));
		if (matched) {
			addrs[count++] = addr;
			if (!found_canon) {
				strncpy(canon, line_canon, canonical_size);
				canon[canonical_size - 1] = '\0';
				found_canon = 1;
			}
		}
	}

	fclose(f);
	return count;
}

struct hostent *
gethostbyname(const char *name)
{
	return gethostbyname2(name, AF_INET);
}

struct hostent *
gethostbyname2(const char *name, int af)
{
	struct in_addr addrs[MAX_ADDRS];
	char canon[256];
	int count;
	struct in_addr numeric_addr;
	int i;

	if (!name) {
		*__h_errno_location() = HOST_NOT_FOUND;
		return NULL;
	}

	/* Always accept AF_INET (and AF_UNSPEC for forwards compat) */
	if (af != AF_INET && af != AF_UNSPEC) {
		*__h_errno_location() = NO_DATA;
		return NULL;
	}

	/* Case 1: Numeric address (e.g. "192.168.1.1") */
	if (inet_pton(AF_INET, name, &numeric_addr) == 1) {
		if (ensure_tls_buf(256 + sizeof(char *) * 2 + sizeof(char *) * 2) != 0)
			return NULL;

		char *p = hostent_data;
		hostent_buf.h_name = p;
		strcpy(p, name);
		p += strlen(name) + 1;

		hostent_buf.h_aliases = (char **)p;
		p += sizeof(char *);
		*(char **)(p - sizeof(char *)) = NULL;

		hostent_buf.h_addrtype = AF_INET;
		hostent_buf.h_length = 4;

		hostent_buf.h_addr_list = (char **)p;
		p += sizeof(char *) * 2;

		struct in_addr *addr_slot = (struct in_addr *)p;
		*addr_slot = numeric_addr;
		hostent_buf.h_addr_list[0] = (char *)addr_slot;
		hostent_buf.h_addr_list[1] = NULL;

		return &hostent_buf;
	}

	/* Case 2: Look up in /etc/hosts */
	canon[0] = '\0';
	count = lookup_name_hosts(name, AF_INET, addrs, MAX_ADDRS,
	                         canon, sizeof(canon));
	if (count < 0) {
		/* h_errno already set by lookup */
		return NULL;
	}
	if (count == 0) {
		*__h_errno_location() = HOST_NOT_FOUND;
		return NULL;
	}

	/* Build hostent from results */
	{
		size_t needed = strlen(canon) + 1
		                + (count + 1) * sizeof(char *)
		                + 2 * sizeof(char *)
		                + count * sizeof(struct in_addr);
		if (ensure_tls_buf(needed) != 0)
			return NULL;

		char *p = hostent_data;
		hostent_buf.h_name = p;
		strcpy(p, canon);
		p += strlen(canon) + 1;

		hostent_buf.h_aliases = (char **)p;
		p += sizeof(char *);
		*(char **)(p - sizeof(char *)) = NULL;

		hostent_buf.h_addrtype = AF_INET;
		hostent_buf.h_length = 4;

		hostent_buf.h_addr_list = (char **)p;
		p += (count + 1) * sizeof(char *);

		struct in_addr *addr_ptr = (struct in_addr *)p;
		for (i = 0; i < count; i++) {
			addr_ptr[i] = addrs[i];
			hostent_buf.h_addr_list[i] = (char *)&addr_ptr[i];
		}
		hostent_buf.h_addr_list[count] = NULL;

		return &hostent_buf;
	}
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
	FILE *f;
	char line[1024];
	char addr_str[64];
	struct in_addr *in = (struct in_addr *)addr;

	if (type != AF_INET || len < 4) {
		*__h_errno_location() = NO_DATA;
		return NULL;
	}

	/* Convert address to string for comparison */
	if (!inet_ntop(AF_INET, in, addr_str, sizeof(addr_str))) {
		*__h_errno_location() = NO_RECOVERY;
		return NULL;
	}

	f = fopen("/etc/hosts", "r");
	if (!f) {
		if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
			*__h_errno_location() = HOST_NOT_FOUND;
			return NULL;
		}
		*__h_errno_location() = NO_RECOVERY;
		return NULL;
	}

	while (fgets(line, sizeof(line), f)) {
		struct in_addr line_addr;
		char canon[256];

		if (match_hosts_line(line, addr_str, NULL,
		                     &line_addr, canon, sizeof(canon))) {
			fclose(f);

			size_t needed = strlen(canon) + 1
			                + 2 * sizeof(char *)
			                + 2 * sizeof(char *)
			                + sizeof(struct in_addr);
			if (ensure_tls_buf(needed) != 0)
				return NULL;

			char *p = hostent_data;
			hostent_buf.h_name = p;
			strcpy(p, canon);
			p += strlen(canon) + 1;

			hostent_buf.h_aliases = (char **)p;
			p += sizeof(char *);
			*(char **)(p - sizeof(char *)) = NULL;

			hostent_buf.h_addrtype = AF_INET;
			hostent_buf.h_length = 4;

			hostent_buf.h_addr_list = (char **)p;
			p += 2 * sizeof(char *);

			struct in_addr *addr_slot = (struct in_addr *)p;
			*addr_slot = *in;
			hostent_buf.h_addr_list[0] = (char *)addr_slot;
			hostent_buf.h_addr_list[1] = NULL;

			return &hostent_buf;
		}
	}

	fclose(f);
	*__h_errno_location() = HOST_NOT_FOUND;
	return NULL;
}

int
gethostbyname_r(const char *name, struct hostent *restrict result,
                char *restrict buf, size_t buflen,
                struct hostent **restrict res, int *restrict h_errnop)
{
	struct in_addr addrs[MAX_ADDRS];
	char canon[256];
	int count;
	struct in_addr numeric_addr;

	if (!name) {
		*h_errnop = HOST_NOT_FOUND;
		*res = NULL;
		return EAI_NONAME;
	}

	/* Numeric address */
	if (inet_pton(AF_INET, name, &numeric_addr) == 1) {
		char *p = buf;
		size_t needed = strlen(name) + 1
		                + sizeof(char *)
		                + 2 * sizeof(char *)
		                + sizeof(struct in_addr);
		if (buflen < needed) {
			*h_errnop = NO_RECOVERY;
			*res = NULL;
			return ERANGE;
		}

		result->h_name = p;
		strcpy(p, name);
		p += strlen(name) + 1;

		result->h_aliases = (char **)p;
		p += sizeof(char *);
		*(char **)(p - sizeof(char *)) = NULL;

		result->h_addrtype = AF_INET;
		result->h_length = 4;

		result->h_addr_list = (char **)p;
		p += 2 * sizeof(char *);

		struct in_addr *addr_slot = (struct in_addr *)p;
		*addr_slot = numeric_addr;
		result->h_addr_list[0] = (char *)addr_slot;
		result->h_addr_list[1] = NULL;

		*res = result;
		*h_errnop = 0;
		return 0;
	}

	/* Look up in /etc/hosts */
	canon[0] = '\0';
	count = lookup_name_hosts(name, AF_INET, addrs, MAX_ADDRS,
	                         canon, sizeof(canon));
	if (count <= 0) {
		*h_errnop = HOST_NOT_FOUND;
		*res = NULL;
		return EAI_NONAME;
	}

	/* Build result in caller's buffer */
	{
		size_t needed = strlen(canon) + 1
		                + sizeof(char *)
		                + (count + 1) * sizeof(char *)
		                + count * sizeof(struct in_addr);
		if (buflen < needed) {
			*h_errnop = NO_RECOVERY;
			*res = NULL;
			return ERANGE;
		}

		char *p = buf;
		result->h_name = p;
		strcpy(p, canon);
		p += strlen(canon) + 1;

		result->h_aliases = (char **)p;
		p += sizeof(char *);
		*(char **)(p - sizeof(char *)) = NULL;

		result->h_addrtype = AF_INET;
		result->h_length = 4;

		result->h_addr_list = (char **)p;
		p += (count + 1) * sizeof(char *);

		struct in_addr *addr_slot = (struct in_addr *)p;
		for (int i = 0; i < count; i++) {
			addr_slot[i] = addrs[i];
			result->h_addr_list[i] = (char *)&addr_slot[i];
		}
		result->h_addr_list[count] = NULL;

		*res = result;
		*h_errnop = 0;
		return 0;
	}
}

int
gethostbyaddr_r(const void *addr, socklen_t len, int type,
                struct hostent *restrict result, char *restrict buf,
                size_t buflen, struct hostent **restrict res,
                int *restrict h_errnop)
{
	FILE *f;
	char line[1024];
	char addr_str[64];
	struct in_addr *in = (struct in_addr *)addr;

	if (type != AF_INET || len < 4) {
		*h_errnop = NO_DATA;
		*res = NULL;
		return EAI_NONAME;
	}

	if (!inet_ntop(AF_INET, in, addr_str, sizeof(addr_str))) {
		*h_errnop = NO_RECOVERY;
		*res = NULL;
		return EAI_SYSTEM;
	}

	f = fopen("/etc/hosts", "r");
	if (!f) {
		if (errno == ENOENT || errno == ENOTDIR || errno == EACCES) {
			*h_errnop = HOST_NOT_FOUND;
			*res = NULL;
			return EAI_NONAME;
		}
		*h_errnop = NO_RECOVERY;
		*res = NULL;
		return EAI_SYSTEM;
	}

	while (fgets(line, sizeof(line), f)) {
		struct in_addr line_addr;
		char canon[256];

		if (match_hosts_line(line, addr_str, NULL,
		                     &line_addr, canon, sizeof(canon))) {
			fclose(f);

			size_t needed = strlen(canon) + 1
			                + 2 * sizeof(char *)
			                + 2 * sizeof(char *)
			                + sizeof(struct in_addr);
			if (buflen < needed) {
				*h_errnop = NO_RECOVERY;
				*res = NULL;
				return ERANGE;
			}

			char *p = buf;
			result->h_name = p;
			strcpy(p, canon);
			p += strlen(canon) + 1;

			result->h_aliases = (char **)p;
			p += sizeof(char *);
			*(char **)(p - sizeof(char *)) = NULL;

			result->h_addrtype = AF_INET;
			result->h_length = 4;

			result->h_addr_list = (char **)p;
			p += 2 * sizeof(char *);

			struct in_addr *addr_slot = (struct in_addr *)p;
			*addr_slot = *in;
			result->h_addr_list[0] = (char *)addr_slot;
			result->h_addr_list[1] = NULL;

			*res = result;
			*h_errnop = 0;
			return 0;
		}
	}

	fclose(f);
	*h_errnop = HOST_NOT_FOUND;
	*res = NULL;
	return EAI_NONAME;
}

/* ---- file iteration helpers (POSIX) ---- */

static _Thread_local FILE *hosts_file;
static _Thread_local int hosts_stayopen;

void
sethostent(int stayopen)
{
	if (hosts_file)
		rewind(hosts_file);
	else
		hosts_file = fopen("/etc/hosts", "r");
	hosts_stayopen = stayopen;
}

void
endhostent(void)
{
	if (hosts_file) {
		fclose(hosts_file);
		hosts_file = NULL;
	}
}

struct hostent *
gethostent(void)
{
	struct in_addr addr;
	char canon[256];
	char line[1024];

	if (!hosts_file)
		sethostent(0);
	if (!hosts_file)
		return NULL;

	while (fgets(line, sizeof(line), hosts_file)) {
		if (match_hosts_line(line, NULL, NULL,
		                     &addr, canon, sizeof(canon))) {
			size_t needed = strlen(canon) + 1
			                + 2 * sizeof(char *)
			                + 2 * sizeof(char *)
			                + sizeof(struct in_addr);
			if (ensure_tls_buf(needed) != 0)
				return NULL;

			char *p = hostent_data;
			hostent_buf.h_name = p;
			strcpy(p, canon);
			p += strlen(canon) + 1;

			hostent_buf.h_aliases = (char **)p;
			p += sizeof(char *);
			*(char **)(p - sizeof(char *)) = NULL;

			hostent_buf.h_addrtype = AF_INET;
			hostent_buf.h_length = 4;

			hostent_buf.h_addr_list = (char **)p;
			p += 2 * sizeof(char *);

			struct in_addr *addr_slot = (struct in_addr *)p;
			*addr_slot = addr;
			hostent_buf.h_addr_list[0] = (char *)addr_slot;
			hostent_buf.h_addr_list[1] = NULL;

			if (!hosts_stayopen)
				endhostent();
			return &hostent_buf;
		}
	}

	if (!hosts_stayopen)
		endhostent();
	return NULL;
}
