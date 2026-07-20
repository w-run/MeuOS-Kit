#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int callback_descriptor;

static void
on_exit(void)
{
	write(callback_descriptor, "E", 1);
}

int
main(void)
{
	int descriptors[2];
	int status;
	pid_t child;
	char byte;
	char temporary[] = "/tmp/meuos-libc-mkstemp-XXXXXX";
	int temporary_fd;

	if (pipe(descriptors) != 0)
		return 1;
	child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		close(descriptors[0]);
		if (write(descriptors[1], "P", 1) != 1)
			_exit(1);
		close(descriptors[1]);
		_exit(37);
	}
	close(descriptors[1]);
	if (read(descriptors[0], &byte, 1) != 1 || byte != 'P' || close(descriptors[0]) != 0)
		return 1;
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 37)
		return 1;
	child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		char *arguments[2];
		char *environment[1];

		arguments[0] = "true";
		arguments[1] = 0;
		environment[0] = 0;
		execve("/bin/true", arguments, environment);
		_exit(127);
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return 1;
	if (pipe(descriptors) != 0)
		return 1;
	child = fork();
	if (child < 0)
		return 1;
	if (child == 0) {
		close(descriptors[0]);
		callback_descriptor = descriptors[1];
		if (atexit(on_exit) != 0)
			_exit(1);
		exit(23);
	}
	close(descriptors[1]);
	if (read(descriptors[0], &byte, 1) != 1 || byte != 'E' || close(descriptors[0]) != 0 || waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 23)
		return 1;
	if (system("exit 19") != (19 << 8))
		return 1;
	temporary_fd = mkstemp(temporary);
	if (temporary_fd < 0 || write(temporary_fd, "ok", 2) != 2
	 || close(temporary_fd) != 0 || unlink(temporary) != 0)
		return 1;
	puts("PASS process syscalls");
	return 0;
}
