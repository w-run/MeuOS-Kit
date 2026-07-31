#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

/* mkstemp(3) - create a unique temporary file from a template
 * Template must end in XXXXXX. */
int
mkstemp(char *template)
{
	size_t len, i;
	int attempt;
	static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";

	/* Locate the trailing X-run once, before the retry loop. */
	len = strlen(template);
	if (len < 6)
		return -1;
	i = len;
	while (i > 0 && template[i - 1] == 'X')
		--i;
	if (len - i != 6)
		return -1;

	/* Try a few different suffixes to find a unique name.  The suffix is
	 * regenerated in the same X positions on every attempt, so a collision
	 * (EEXIST) is actually retried instead of failing after one try. */
	for (attempt = 0; attempt < 1024; ++attempt) {
		unsigned long seed = ((unsigned long)getpid() << 16)
			^ (unsigned long)time(NULL) ^ (unsigned long)attempt;
		int j, fd;

		for (j = 0; j < 6; ++j) {
			seed = seed * 1103515245ul + 12345ul;
			template[i + j] = letters[(seed >> 16) % 36];
		}
		fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
			return fd;
		/* If the file already exists, try again */
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}
