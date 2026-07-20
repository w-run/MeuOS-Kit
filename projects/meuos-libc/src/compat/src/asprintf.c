#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>

/* asprintf / vasprintf -- POSIX.1-2008 (originally a glibc/BSD extension).
 * Two-pass: compute length via vsnprintf(NULL,0,...), allocate, format. */
int
vasprintf(char **strp, const char *format, va_list ap)
{
	va_list ap2;
	int len;
	char *buf;

	va_copy(ap2, ap);
	len = vsnprintf(NULL, 0, format, ap2);
	va_end(ap2);
	if (len < 0)
		return -1;
	buf = malloc((size_t)len + 1);
	if (!buf)
		return -1;
	vsnprintf(buf, (size_t)len + 1, format, ap);
	buf[len] = '\0';
	*strp = buf;
	return len;
}

int
asprintf(char **strp, const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vasprintf(strp, format, ap);
	va_end(ap);
	return ret;
}
