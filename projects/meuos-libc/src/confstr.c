#include <errno.h>
#include <string.h>
#include <unistd.h>

/* _CS_* name constants (subset). */
#ifndef _CS_PATH
#define _CS_PATH 1
#endif
#ifndef _CS_GNU_LIBC_VERSION
#define _CS_GNU_LIBC_VERSION 2
#endif

size_t
confstr(int name, char *buf, size_t len)
{
	const char *val;
	size_t n;

	switch (name) {
	case _CS_PATH:
		/* POSIX requires this; mirrors a conventional PATH. */
		val = "/bin:/usr/bin";
		break;
	case _CS_GNU_LIBC_VERSION:
		/* Report the MeuOS libc ABI rather than a GNU-specific tag. */
		val = "meuos-libc";
		break;
	default:
		errno = EINVAL;
		return 0;
	}

	n = strlen(val) + 1;   /* include trailing NUL */
	if (buf != NULL && len != 0) {
		size_t copy = n < len ? n : len;
		memcpy(buf, val, copy);
		if (copy == len)
			buf[len - 1] = '\0';
	}
	return n;
}
