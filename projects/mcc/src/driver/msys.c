/* msys.c — .msys single-file sysroot utilities for mcc.
 *
 * Provides: detect .msys suffix, open a .msys file and return a
 * VFS handle so the rest of the driver can read files via
 * msys_fopen() instead of extracting to a temp directory. */

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
