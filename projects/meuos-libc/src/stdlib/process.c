#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int
system(const char *command)
{
	pid_t child;
	int status;
	char *arguments[] = { "sh", "-c", (char *)command, NULL };

	if (!command)
		return 1;
	child = fork();
	if (child < 0)
		return -1;
	if (!child) {
		execve("/bin/sh", arguments, environ);
		_exit(127);
	}
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return status;
}

int
mkstemp(char *template)
{
	static unsigned long sequence;
	size_t length;
	char *suffix;
	unsigned long value;
	int attempt;
	int fd;

	if (!template) {
		errno = EINVAL;
		return -1;
	}
	length = strlen(template);
	if (length < 6 || memcmp(template + length - 6, "XXXXXX", 6) != 0) {
		errno = EINVAL;
		return -1;
	}
	suffix = template + length - 6;
	for (attempt = 0; attempt < 128; ++attempt) {
		int i;
		value = ((unsigned long)getpid() << 16) ^ ++sequence ^ (unsigned long)attempt;
		for (i = 5; i >= 0; --i) {
			suffix[i] = "abcdefghijklmnopqrstuvwxyz0123456789"[value % 36];
			value /= 36;
		}
		fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	errno = EEXIST;
	return -1;
}
