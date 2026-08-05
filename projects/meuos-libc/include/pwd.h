#ifndef MEUOS_PWD_H
#define MEUOS_PWD_H

#include <sys/types.h>

/* POSIX <pwd.h>: user database entry.  MeuOS provides a minimal file-based
 * backing (parse /etc/passwd), not full NSS. */
struct passwd {
	char    *pw_name;       /* user name */
	char    *pw_passwd;     /* encrypted password */
	uid_t    pw_uid;        /* user ID */
	gid_t    pw_gid;        /* group ID */
	char    *pw_gecos;      /* real name / comment */
	char    *pw_dir;        /* home directory */
	char    *pw_shell;      /* shell program */
};

struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uid_t uid);

#endif /* MEUOS_PWD_H */
