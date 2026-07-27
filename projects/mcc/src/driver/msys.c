/* msys.c — .msys single-file sysroot utilities for mcc.
 *
 * Provides: detect .msys suffix, open a .msys file and return a
 * VFS handle so the rest of the driver can read files via
 * msys_fopen() instead of extracting to a temp directory.
 *
 * Also handles @meuos_arch metadata extraction for auto-detecting
 * the target architecture from a .msys sysroot. */

#include "mt/msys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check if a path ends with ".msys". */
int
msys_is_sysroot(const char *path)
{
	size_t len = path ? strlen(path) : 0;
	return len >= 5 && strcmp(path + len - 5, ".msys") == 0;
}

/* Open a .msys file and return a VFS handle.
 * Returns the msys handle on success, or NULL on error. */
struct msys *
msys_sysroot_open(const char *sysroot_path)
{
	if (!msys_is_sysroot(sysroot_path))
		return NULL;
	return msys_open(sysroot_path);
}

/* Extract the target architecture from @meuos_arch metadata in a .msys
 * archive.  Returns a malloc'd string (caller must free), or NULL if
 * the metadata is absent or the read fails. */
char *
msys_sysroot_get_arch(struct msys *m)
{
	size_t dsize;
	const void *data;
	if (!m)
		return NULL;
	data = msys_search(m, "@meuos_arch", &dsize);
	if (!data || dsize == 0)
		return NULL;
	return strndup((const char *)data, dsize);
}

/* Return the number of standard VFS include prefixes for a .msys sysroot.
 * These are archive-relative paths (e.g. "include", "usr/include") that
 * a preprocessor can use with msys_fopen() without constructing fake
 * filesystem paths.
 *
 * Returns the number of prefixes written to prefixes[] (max max_count). */
int
msys_sysroot_incprefixes(const char *prefixes[], int max_count)
{
	static const char *vfs_prefixes[] = {"include", "usr/include"};
	int n = sizeof(vfs_prefixes) / sizeof(vfs_prefixes[0]);
	if (max_count < n) n = max_count;
	for (int i = 0; i < n; i++)
		prefixes[i] = vfs_prefixes[i];
	return n;
}
