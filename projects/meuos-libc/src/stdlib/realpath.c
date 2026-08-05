/* stdlib/realpath.c — POSIX canonical path resolution.
 *
 * Resolves path to a canonical absolute pathname: makes relative inputs
 * absolute against the cwd, collapses '.' and repeated '/', resolves '..'
 * and symbolic links (each symlink is expanded; a symlink loop is bounded
 * by SYMLOOP_MAX).  If resolved is NULL, the result is malloc'd (POSIX
 * extension, used by canonicalize_file_name); the caller must free it.
 *
 * Zero GNU dependency; realpath is POSIX.1-2008 and lives in core libc.  The
 * ported algorithm is the standard component-stack resolver (cf. musl). */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef SYMLOOP_MAX
#define SYMLOOP_MAX 40
#endif

/* component stack: res[0..rlen) is the accumulated canonical path. */
static int
push_component(char *res, size_t *rlen, const char *comp, const char **endp)
{
	size_t clen = *endp - comp;
	if (clen == 0 || (clen == 1 && comp[0] == '.'))
		return 0; /* empty or '.' */
	if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
		/* pop one component */
		if (*rlen == 1) {
			/* at root: '..' is a no-op */
			return 0;
		}
		/* back up past the last '/' */
		while (*rlen > 1 && res[*rlen - 1] != '/')
			(*rlen)--;
		if (*rlen > 1)
			(*rlen)--; /* remove the '/' */
		res[*rlen] = '/';
		return 0;
	}
	/* normal component: ensure separator then append */
	size_t need = (*rlen == 1 ? 1 : 1 /*'/'*/) + clen;
	if (*rlen + need >= PATH_MAX)
		return -1;
	if (*rlen > 1)
		res[(*rlen)++] = '/';
	memcpy(res + *rlen, comp, clen);
	*rlen += clen;
	res[*rlen] = 0;
	return 0;
}

char *
realpath(const char *path, char *resolved)
{
	char *out;
	int out_is_malloc = 0;
	static char mbuf[PATH_MAX];
	char work[PATH_MAX];       /* current path being resolved (may be symlink) */
	char sym[PATH_MAX];
	int symloop = 0;
	size_t rlen;

	if (!path) {
		errno = EINVAL;
		return NULL;
	}

	out = resolved ? resolved : mbuf;
	if (!resolved) {
		out = malloc(PATH_MAX);
		if (!out) {
			errno = ENOMEM;
			return NULL;
		}
		out_is_malloc = 1;
	}

	if (path[0] != '/') {
		/* relative: prepend cwd */
		if (!getcwd(work, PATH_MAX))
			goto fail;
		size_t wl = strlen(work);
		if (wl + 1 + strlen(path) + 1 >= PATH_MAX)
			goto toolong;
		work[wl++] = '/';
		strcpy(work + wl, path);
	} else {
		strncpy(work, path, PATH_MAX - 1);
		work[PATH_MAX - 1] = 0;
	}

	while (1) {
		/* try to resolve `work` through symlinks: if the head component chain
		 * points to a symlink, expand it; else readlink on the full path. */
		ssize_t n = readlink(work, sym, sizeof sym - 1);
		if (n < 0) {
			if (errno == EINVAL)
				break; /* not a symlink; proceed to collapse */
			if (errno == ENOENT || errno == ENOTDIR) {
				/* path doesn't exist: POSIX permits returning the collapses
				 * form; but we mirror glibc by failing */
				goto fail;
			}
			if (errno == ENAMETOOLONG)
				goto toolong;
			/* other errors: let it through */
			break;
		}
		sym[n] = 0;
		if (++symloop > SYMLOOP_MAX) {
			errno = ELOOP;
			goto fail;
		}
		/* expand: dir(work) + sym, or sym if absolute */
		char *slash = strrchr(work, '/');
		if (sym[0] == '/') {
			strncpy(work, sym, PATH_MAX - 1);
			work[PATH_MAX - 1] = 0;
		} else if (slash && slash != work) {
			*slash = 0;
			size_t dl = strlen(work);
			if (dl + 1 + strlen(sym) + 1 >= PATH_MAX)
				goto toolong;
			work[dl++] = '/';
			strcpy(work + dl, sym);
		} else if (slash == work) {
			/* root-relative symlink */
			if (1 + strlen(sym) + 1 >= PATH_MAX)
				goto toolong;
			work[1] = 0;
			strcat(work, sym);
		} else {
			/* no '/': symlink target is relative to root? all our work is abs */
			strncpy(work, sym, PATH_MAX - 1);
			work[PATH_MAX - 1] = 0;
		}
	}

	/* collapse '.', '..', '//' in work into out */
	out[0] = '/';
	out[1] = 0;
	rlen = 1;
	{
		const char *p = work;
		while (*p) {
			if (*p == '/') {
				p++;
				continue;
			}
			const char *start = p;
			while (*p && *p != '/')
				p++;
			if (push_component(out, &rlen, start, &p) < 0)
				goto toolong;
		}
	}
	/* ensure trailing '/' for directories is not double; keep single root */
	{
		size_t ol = strlen(out);
		if (ol > 1 && out[ol - 1] == '/')
			out[--ol] = 0;
	}

	return out;

toolong:
	errno = ENAMETOOLONG;
fail:
	if (out_is_malloc)
		free(out);
	return NULL;
}
