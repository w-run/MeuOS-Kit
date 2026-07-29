/* stdio/file_misc.c -- miscellaneous stdio: tmpfile/tmpnam/remove/perror.
 *
 * perror uses printf + strerror; tmpnam produces /tmp/tmp<n> names;
 * tmpfile opens an anonymous file that gets unlinked immediately;
 * remove prefers unlink() and falls back to rmdir() for directories. */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

int
remove(const char *path)
{
	if (unlink(path) < 0)
		return rmdir(path);
	return 0;
}

char *
tmpnam(char *s)
{
	static char buf[L_tmpnam];
	static unsigned long counter;
	char *b = s ? s : buf;

	snprintf(b, L_tmpnam, "/tmp/tmp%lu", counter++);
	return b;
}

FILE *
tmpfile(void)
{
	char name[L_tmpnam];
	int fd;
	FILE *f;

	snprintf(name, sizeof(name), "/tmp/meuostmp%lu", (unsigned long)getpid());
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return NULL;
	unlink(name);
	f = fdopen(fd, "w+");
	if (!f) {
		close(fd);
		return NULL;
	}
	return f;
}

void
perror(const char *prefix)
{
	if (prefix && *prefix)
		printf("%s: %s\n", prefix, strerror(errno));
	else
		printf("%s\n", strerror(errno));
}
