#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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
		char *envp[] = {NULL};
		execve("/bin/sh", arguments, environ ? environ : envp);
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

int
execv(const char *path, char *const arguments[])
{
	char *environment[] = { NULL };

	return execve(path, arguments, environ ? environ : environment);
}

/*
 * execvp：file 含 '/' 时直接执行；否则按 PATH 依次查找（PATH 未设置时
 * 默认 "/usr/bin:/bin"，空项表示当前目录）。按 POSIX 语义，若某候选存在
 * 但不可执行（EACCES 等），继续搜索并让该 errno 优先于 ENOENT 返回。
 */
int
execvp(const char *file, char *const arguments[])
{
	const char *path;
	char *copy, *dir, *colon;
	size_t file_len;
	int candidate_errno;
	int saved_errno = ENOENT;

	if (!file || !*file) {
		errno = ENOENT;
		return -1;
	}
	if (strchr(file, '/'))
		return execv(file, arguments);
	file_len = strlen(file);
	path = getenv("PATH");
	if (!path || !*path)
		path = "/usr/bin:/bin";
	copy = strdup(path);
	if (!copy)
		return -1;

	dir = copy;
	colon = strchr(dir, ':');
	if (colon)
		*colon = '\0';
	for (;;) {
		char *candidate;
		size_t dir_len = strlen(dir);

		candidate = malloc(dir_len + 1 + file_len + 1);
		if (!candidate) {
			free(copy);
			return -1;
		}
		if (dir_len)
			snprintf(candidate, dir_len + file_len + 2, "%s/%s", dir, file);
		else
			memcpy(candidate, file, file_len + 1);
		execv(candidate, arguments);
		candidate_errno = errno;
		free(candidate);
		if (candidate_errno != ENOENT)
			saved_errno = candidate_errno;
		if (!colon)
			break;
		dir = colon + 1;
		colon = strchr(dir, ':');
		if (colon)
			*colon = '\0';
	}
	free(copy);
	errno = saved_errno;
	return -1;
}
