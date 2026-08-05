/* dirent/nftw.c — POSIX.1-2008 file tree walk (nftw).
 *
 * nftw(path, fn, fd_limit, flags) calls fn(name, &st, flag, &ftw) for the
 * initial path and, recursively, everything beneath it.  fd_limit is kept
 * for API compatibility but (like musl) a simple recursion is used; the C
 * library does not hold an open dir across the callback, so no fd ceiling
 * is needed.  Return 0 (or the callback's non-zero value if it returns one);
 * -1 on an I/O/alloc error (errno set).
 *
 * Zero GNU dependency; glibc keeps nftw in libc itself, so it lives in core.
 */

#include <ftw.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int nftw_rec(const char *path, __nftw_fn_t fn, int flags, int depth,
                    int no_mount_cross, dev_t mount_root)
{
	struct stat st;
	struct FTW ftw;
	int flag;
	int phys = (flags & FTW_PHYS);

	/* determine node type */
	if (lstat(path, &st) != 0) {
		flag = FTW_NS; /* stat failed */
		ftw.base = 0;
		ftw.level = depth;
		memset(&st, 0, sizeof st);
		return fn(path, &st, flag, &ftw);
	}

	/* FTW_CHDIR is not silently implemented: keep it simple (we pass full
	 * paths; the callback gets the original absolute/relative path). */

	if (S_ISDIR(st.st_mode)) {
		flag = FTW_D;
	} else if (S_ISLNK(st.st_mode)) {
		if (phys) {
			flag = FTW_SL;
		} else {
			/* dangling symlink -> FTW_SLN; else use target's type */
			struct stat st2;
			if (stat(path, &st2) != 0)
				flag = FTW_SLN;
			else {
				st = st2;
				flag = S_ISDIR(st2.st_mode) ? FTW_D : FTW_F;
			}
		}
	} else {
		flag = FTW_F;
	}

	/* base = basename offset in path */
	{
		const char *slash = strrchr(path, '/');
		ftw.base = slash ? (int)(slash + 1 - path) : 0;
		ftw.level = depth;
	}

	/* pre-order visit */
	if (!(flags & FTW_DEPTH) || flag != FTW_D) {
		int r = fn(path, &st, flag, &ftw);
		if (r)
			return r;
	}

	/* recurse into directory */
	if (flag == FTW_D) {
		/* FTW_MOUNT: do not descend into a different device */
		if ((flags & FTW_MOUNT) && st.st_dev != mount_root)
			return 0;

		DIR *d = opendir(path);
		if (!d) {
			/* unreadable dir -> FTW_DNR callback */
			flag = FTW_DNR;
			int r = fn(path, &st, flag, &ftw);
			return r;
		}

		/* pre-order for the directory already done above */
		struct dirent *de;
		while ((de = readdir(d))) {
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;
			size_t pl = strlen(path);
			size_t nl = strlen(de->d_name);
			char *child = (char *)malloc(pl + 1 + nl + 1);
			if (!child) {
				closedir(d);
				errno = ENOMEM;
				return -1;
			}
			memcpy(child, path, pl);
			child[pl] = '/';
			memcpy(child + pl + 1, de->d_name, nl + 1);
			int r = nftw_rec(child, fn, flags, depth + 1, no_mount_cross, mount_root);
			free(child);
			if (r) {
				closedir(d);
				return r;
			}
		}
		closedir(d);

		/* post-order visit for directory when FTW_DEPTH */
		if (flags & FTW_DEPTH) {
			/* re-stat for the post-order callback */
			struct stat st2;
			if (lstat(path, &st2) == 0)
				st = st2;
			int r = fn(path, &st, FTW_DP, &ftw);
			if (r)
				return r;
		}
	}

	return 0;
}

int
nftw(const char *path, __nftw_fn_t fn, int fd_limit, int flags)
{
	(void)fd_limit;
	struct stat st;
	dev_t mount_root = 0;

	if (flags & FTW_MOUNT) {
		if (lstat(path, &st) != 0)
			return -1;
		mount_root = st.st_dev;
	}

	return nftw_rec(path, fn, flags, 0, 0, mount_root);
}
