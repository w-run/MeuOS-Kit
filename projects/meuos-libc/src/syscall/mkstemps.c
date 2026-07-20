#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/* mkstemps(3) - create a unique temporary file from a template
 * Template must end in XXXXXX[.suffix]. The suffix length is the
 * number of chars after the last run of X. */
int
mkstemps(char *template, int suffixlen)
{
	size_t len = strlen(template);
	if (suffixlen < 0 || (size_t)suffixlen > len)
		return -1;
	/* Find the end of the X-run */
	size_t end = len - (size_t)suffixlen;
	size_t i = end;
	while (i > 0 && template[i - 1] == 'X')
		--i;
	if (i == end)
		return -1;
	/* Replace template[i..end) with "XXXXXX" */
	if (end - i != 6)
		return -1;
	memcpy(template + i, "XXXXXX", 6);
	return mkstemp(template);
}
