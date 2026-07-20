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
	/* Try a few different suffixes to find a unique name */
	for (int attempt = 0; attempt < 1024; ++attempt) {
		size_t len = strlen(template);
		if (len < 6)
			return -1;
		/* Find X-run at end */
		size_t i = len;
		while (i > 0 && template[i - 1] == 'X')
			--i;
		if (i == len || len - i != 6)
			return -1;
		/* Generate 6 random chars using PID + time + attempt */
		unsigned long seed = ((unsigned long)getpid() << 16) ^ (unsigned long)time(NULL) ^ (unsigned long)attempt;
		static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";
		for (int j = 0; j < 6; ++j) {
			seed = seed * 1103515245ul + 12345ul;
			template[i + j] = letters[(seed >> 16) % 36];
		}
		int fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
			return fd;
		/* If the file already exists, try again */
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}
