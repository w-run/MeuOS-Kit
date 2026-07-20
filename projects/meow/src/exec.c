/* meow - command execution and package discovery. */
#include <stdio.h>
#include <stdlib.h>
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
	child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		execve("/bin/sh", arguments, environ);
		_exit(127);
	}
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

