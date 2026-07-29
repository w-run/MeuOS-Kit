/* meow - command execution and package discovery. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meow.h"

extern char **environ;

int
list_packages(void)
{
	DIR *dir = opendir("pkgs");
	struct dirent *entry;

	if (!dir)
		return -1;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] != '.')
			printf("%s\n", entry->d_name);
	}
	closedir(dir);
	return 0;
}

int
run(const char *command)
{
	pid_t child;
	int status;
	char *arguments[4];
	char complete[RECIPE_ENV_MAX + RECIPE_MAX];
	size_t environment_length = strlen(recipe_environment);
	size_t command_length = strlen(command);

	if (environment_length + command_length + 1 > sizeof(complete))
		return -1;
	memcpy(complete, recipe_environment, environment_length);
	memcpy(complete + environment_length, command, command_length + 1);
	arguments[0] = "sh";
	arguments[1] = "-c";
	arguments[2] = complete;
	arguments[3] = 0;

	/* Capture child output unless verbose/debug mode.  In quiet (default)
	 * mode, only print it if the command fails. */
	int capture_output = (g_output_mode != OUTPUT_VERBOSE &&
	                      g_output_mode != OUTPUT_DEBUG);
	int out_pipe[2] = {-1, -1};
	int err_pipe[2] = {-1, -1};
	char *out_buf = NULL;
	char *err_buf = NULL;
	size_t out_cap = 0, out_len = 0;
	size_t err_cap = 0, err_len = 0;

	if (capture_output) {
		if (pipe(out_pipe) != 0) capture_output = 0;
		else if (pipe(err_pipe) != 0) {
			close(out_pipe[0]); close(out_pipe[1]);
			capture_output = 0;
		}
	}

	child = fork();
	if (child < 0) {
		if (capture_output) {
			close(out_pipe[0]); close(out_pipe[1]);
			close(err_pipe[0]); close(err_pipe[1]);
		}
		return -1;
	}
	if (child == 0) {
		if (capture_output) {
			dup2(out_pipe[1], 1);
			dup2(err_pipe[1], 2);
			close(out_pipe[0]); close(out_pipe[1]);
			close(err_pipe[0]); close(err_pipe[1]);
		}
		execve("/bin/sh", arguments, environ);
		_exit(127);
	}
	if (capture_output) {
		close(out_pipe[1]); close(err_pipe[1]);
		/* Read both pipes until EOF. */
		out_cap = 65536; out_buf = malloc(out_cap);
		err_cap = 65536; err_buf = malloc(err_cap);
		out_len = err_len = 0;
		int out_eof = 0, err_eof = 0;
		fd_set rfds;
		while (!out_eof || !err_eof) {
			FD_ZERO(&rfds);
			if (!out_eof) FD_SET(out_pipe[0], &rfds);
			if (!err_eof) FD_SET(err_pipe[0], &rfds);
			int maxfd = out_pipe[0] > err_pipe[0] ? out_pipe[0] : err_pipe[0];
			if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0)
				break;
			if (!out_eof && FD_ISSET(out_pipe[0], &rfds)) {
				if (out_len + 4096 > out_cap) {
					if (out_cap > SIZE_MAX / 2) { free(out_buf); free(err_buf); return -1; }
					out_cap *= 2;
					char *new_buf = realloc(out_buf, out_cap);
					if (!new_buf) { free(out_buf); free(err_buf); return -1; }
					out_buf = new_buf;
				}
				ssize_t n = read(out_pipe[0], out_buf + out_len, 4096);
				if (n <= 0) out_eof = 1; else out_len += n;
			}
			if (!err_eof && FD_ISSET(err_pipe[0], &rfds)) {
				if (err_len + 4096 > err_cap) {
					if (err_cap > SIZE_MAX / 2) { free(out_buf); free(err_buf); return -1; }
					err_cap *= 2;
					char *new_buf = realloc(err_buf, err_cap);
					if (!new_buf) { free(out_buf); free(err_buf); return -1; }
					err_buf = new_buf;
				}
				ssize_t n = read(err_pipe[0], err_buf + err_len, 4096);
				if (n <= 0) err_eof = 1; else err_len += n;
			}
		}
		close(out_pipe[0]); close(err_pipe[0]);
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
		free(out_buf); free(err_buf);
		return -1;
	}
	int rc = WEXITSTATUS(status);
	if (capture_output && rc != 0) {
		/* Print captured output on failure. */
		if (out_len > 0) fwrite(out_buf, 1, out_len, stdout);
		if (err_len > 0) fwrite(err_buf, 1, err_len, stderr);
	}
	free(out_buf); free(err_buf);
	return rc;
}

