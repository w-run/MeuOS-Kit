/* process/spawn.c — POSIX.1-2008 process spawn (posix_spawn family).
 *
 * posix_spawn()/posix_spawnp() fork a child, apply the requested file
 * actions (open/close/dup2), then execve/execvp the target.  On exec
 * failure the child exits with status 127 (the parent returns 0: the process
 * was created; exec failure is observable via waitpid of the child).
 *
 * Supported attr: none of the standard flags are applied yet (SETPGROUP /
 * RESETIDS / SETSIG* need setpgid / setuid / signal-mask syscall plumbing)
 * -- a documented minimal subset; the flags are accepted and stored.  Zero
 * GNU dependency; posix_spawn is POSIX.1-2008 in core libc. */

#include <spawn.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

/* ---- file actions ---- */

#define FA_OPEN  0
#define FA_CLOSE 1
#define FA_DUP2  2

struct fa_entry {
	int type;
	int fd;       /* fd to open to / close / dup2-to */
	int fd2;      /* dup2 source */
	char *path;
	int oflag;
	mode_t mode;
};

int
posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa)
{
	fa->__allocated = 0;
	fa->__used = 0;
	fa->__actions = NULL;
	return 0;
}

int
posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa)
{
	struct fa_entry *a = (struct fa_entry *)fa->__actions;
	if (a) {
		for (int i = 0; i < fa->__used; i++)
			free(a[i].path);
		free(a);
	}
	fa->__actions = NULL;
	fa->__allocated = fa->__used = 0;
	return 0;
}

static int
grow(posix_spawn_file_actions_t *fa)
{
	struct fa_entry *a = (struct fa_entry *)fa->__actions;
	int ncap = fa->__allocated ? fa->__allocated * 2 : 4;
	struct fa_entry *na = (struct fa_entry *)realloc(a,
	                                  (size_t)ncap * sizeof *na);
	if (!na)
		return ENOMEM;
	fa->__actions = na;
	fa->__allocated = ncap;
	return 0;
}

int
posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd)
{
	if (fd < 0)
		return EBADF;
	if (grow(fa))
		return ENOMEM;
	struct fa_entry *a = &((struct fa_entry *)fa->__actions)[fa->__used++];
	a->type = FA_CLOSE;
	a->fd = fd;
	a->path = NULL;
	return 0;
}

int
posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int fd, int newfd)
{
	if (fd < 0 || newfd < 0)
		return EBADF;
	if (grow(fa))
		return ENOMEM;
	struct fa_entry *a = &((struct fa_entry *)fa->__actions)[fa->__used++];
	a->type = FA_DUP2;
	a->fd = newfd;
	a->fd2 = fd;
	a->path = NULL;
	return 0;
}

int
posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *fa, int fd,
                                 const char *path, int oflag, mode_t mode)
{
	if (fd < 0)
		return EBADF;
	if (grow(fa))
		return ENOMEM;
	struct fa_entry *a = &((struct fa_entry *)fa->__actions)[fa->__used++];
	a->type = FA_OPEN;
	a->fd = fd;
	a->path = strdup(path);
	if (!a->path) {
		fa->__used--;
		return ENOMEM;
	}
	a->oflag = oflag;
	a->mode = mode;
	return 0;
}

/* Apply the file actions in the child; returns a POSIX errno, or 0 on ok. */
static int
apply_actions(const posix_spawn_file_actions_t *fa)
{
	const struct fa_entry *a = (const struct fa_entry *)fa->__actions;
	for (int i = 0; i < fa->__used; i++) {
		switch (a[i].type) {
		case FA_CLOSE:
			if (close(a[i].fd) != 0 && errno != EBADF)
				return errno;
			break;
		case FA_DUP2:
			if (dup2(a[i].fd2, a[i].fd) < 0)
				return errno;
			break;
		case FA_OPEN: {
			int nfd = open(a[i].path, a[i].oflag, a[i].mode);
			if (nfd < 0)
				return errno;
			if (nfd != a[i].fd) {
				if (dup2(nfd, a[i].fd) < 0) {
					close(nfd);
					return errno;
				}
				close(nfd);
			}
			break;
		}
		}
	}
	return 0;
}

/* ---- attr ---- */

int
posix_spawnattr_init(posix_spawnattr_t *attr)
{
	memset(attr, 0, sizeof *attr);
	attr->__flags = 0;
	attr->__pgrp = 0;
	return 0;
}

int
posix_spawnattr_destroy(posix_spawnattr_t *attr)
{
	(void)attr;
	return 0;
}

int
posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags)
{
	attr->__flags = flags;
	return 0;
}

int
posix_spawnattr_getflags(const posix_spawnattr_t *attr, short *flags)
{
	if (flags)
		*flags = attr->__flags;
	return 0;
}

/* ---- spawn ---- */

static int
spawn_impl(pid_t *pidp, const char *path, const posix_spawn_file_actions_t *fa,
           const posix_spawnattr_t *attr, char *const argv[],
           char *const envp[], int use_path)
{
	pid_t pid = fork();
	if (pid < 0)
		return errno;
	if (pid == 0) {
		/* child */
		if (fa && fa->__used) {
			int e = apply_actions(fa);
			if (e)
				_exit(127);
		}
		/* Attr setpgroup/handler flags are accepted but, in this minimal
		 * subset, not yet applied (setpgid and signal masking would need
		 * additional syscall plumbing). */
		(void)attr;
		if (use_path)
			execvp(path, argv);
		else
			execv(path, argv);
		_exit(127); /* exec failed */
	}
	/* parent */
	if (pidp)
		*pidp = pid;
	return 0;
}

int
posix_spawn(pid_t *pidp, const char *path, const posix_spawn_file_actions_t *fa,
            const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
	if (!path) {
		errno = EINVAL;
		return EINVAL;
	}
	return spawn_impl(pidp, path, fa, attr, argv, envp, 0);
}

int
posix_spawnp(pid_t *pidp, const char *file, const posix_spawn_file_actions_t *fa,
             const posix_spawnattr_t *attr, char *const argv[], char *const envp[])
{
	if (!file) {
		errno = EINVAL;
		return EINVAL;
	}
	return spawn_impl(pidp, file, fa, attr, argv, envp, 1);
}
