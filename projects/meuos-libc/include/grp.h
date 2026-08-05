#ifndef MEUOS_GRP_H
#define MEUOS_GRP_H

#include <sys/types.h>

/* POSIX <grp.h>: group database entry.  Minimal file-based backing
 * (parse /etc/group), not full NSS. */
struct group {
	char    *gr_name;       /* group name */
	char    *gr_passwd;     /* encrypted password */
	gid_t    gr_gid;        /* group ID */
	char   **gr_mem;        /* member list (NULL-terminated) */
};

struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);

#endif /* MEUOS_GRP_H */
