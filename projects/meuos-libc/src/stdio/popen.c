/* stdio/popen.c -- popen(3) / pclose(3).
 *
 * popen 创建管道并在子进程中执行 "/bin/sh -c command"。mode "r" 时子进程
 * 写管道、父进程读（FILE 为只读）；"w" 时反向。子进程 pid 保存在 FILE 的
 * pid 字段中，供 pclose 回收。 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "internal.h"

FILE *
popen(const char *command, const char *mode)
{
	int pipefd[2];
	pid_t pid;
	int parent_fd, child_fd;
	FILE *stream;

	if (!command || !mode ||
	    (mode[0] != 'r' && mode[0] != 'w') || mode[1] != '\0') {
		errno = EINVAL;
		return NULL;
	}
	if (pipe(pipefd) < 0)
		return NULL;
	if (mode[0] == 'r') {
		parent_fd = pipefd[0];
		child_fd = pipefd[1];
	} else {
		parent_fd = pipefd[1];
		child_fd = pipefd[0];
	}
	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return NULL;
	}
	if (pid == 0) {
		char *arguments[] = { "sh", "-c", (char *)command, NULL };

		dup2(child_fd, mode[0] == 'r' ? 1 : 0);
		close(pipefd[0]);
		close(pipefd[1]);
		execv("/bin/sh", arguments);
		_exit(127);
	}
	close(child_fd);
	stream = fdopen(parent_fd, mode);
	if (!stream) {
		close(parent_fd);
		(void)waitpid(pid, NULL, 0);
		return NULL;
	}
	((struct __meuos_FILE *)stream)->pid = pid;
	return stream;
}

int
pclose(FILE *stream)
{
	int status;
	pid_t pid;

	if (!stream) {
		errno = EINVAL;
		return -1;
	}
	pid = ((struct __meuos_FILE *)stream)->pid;
	if (pid <= 0) {
		errno = EINVAL;
		return -1;
	}
	((struct __meuos_FILE *)stream)->pid = 0;
	if (fclose(stream) != 0)
		return -1;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return status;
}
