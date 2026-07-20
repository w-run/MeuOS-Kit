/* meow - dependency-graph execution.
 *
 * run_target() is the public entry; target_out_of_date(), file_mtime(),
 * newer(), expand_command() and append_text() are file-local helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meow.h"

/* File-local helpers (defined below; run_target() calls them). */
static int target_out_of_date(struct target *);
static int expand_command(struct target *, const char *, char *, size_t);

int
run_target(struct target *target)
{
	if (!target || target->visiting)
		return -1;
	if (target->done)
		return 0;
	target->visiting = 1;
	if (parallel_jobs > 1) {
		pid_t children[TARGET_DEPS_MAX];
		struct target *child_targets[TARGET_DEPS_MAX];
		size_t active = 0;
		int failed = 0;

		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (!dependency)
				continue;
			while (active == (size_t)parallel_jobs) {
				int status;
				pid_t done = waitpid(-1, &status, 0);
				for (size_t j = 0; j < active; ++j)
					if (children[j] == done) {
						child_targets[j]->done = WIFEXITED(status) && WEXITSTATUS(status) == 0;
						if (!child_targets[j]->done) failed = 1;
						children[j] = children[--active];
						child_targets[j] = child_targets[active];
						break;
					}
				if (done < 0) return -1;
			}
			children[active] = fork();
			if (children[active] < 0)
				return -1;
			if (!children[active])
				_exit(run_target(dependency) == 0 ? 0 : 1);
			child_targets[active++] = dependency;
		}
		while (active) {
			int status;
			pid_t done = waitpid(-1, &status, 0);
			for (size_t j = 0; j < active; ++j)
				if (children[j] == done) {
					child_targets[j]->done = WIFEXITED(status) && WEXITSTATUS(status) == 0;
					if (!child_targets[j]->done) failed = 1;
					children[j] = children[--active];
					child_targets[j] = child_targets[active];
					break;
				}
			if (done < 0) return -1;
		}
		if (failed)
			return -1;
	} else {
		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (dependency && run_target(dependency) != 0)
				return -1;
		}
	}
	if (target_out_of_date(target))
		for (size_t i = 0; i < target->ncommands; ++i)
			{
				char command[RECIPE_MAX];
				if (expand_command(target, target->commands[i], command,
					sizeof(command)) != 0 || run(command) != 0)
				return -1;
			}
	target->visiting = 0;
	target->done = 1;
	return 0;
}

static int
file_mtime(const char *path, long *seconds, long *nanoseconds)
{
	struct stat status;

	if (stat(path, &status) < 0)
		return -1;
	*seconds = status.st_mtim.tv_sec;
	*nanoseconds = status.st_mtim.tv_nsec;
	return 0;
}

static void
newer(long seconds, long nanoseconds, long *latest_seconds, long *latest_nanoseconds)
{
	if (seconds > *latest_seconds ||
	 (seconds == *latest_seconds && nanoseconds > *latest_nanoseconds)) {
		*latest_seconds = seconds;
		*latest_nanoseconds = nanoseconds;
	}
}

static int
target_out_of_date(struct target *target)
{
	long newest_seconds = 0;
	long newest_nanoseconds = 0;
	long oldest_seconds = 0;
	long oldest_nanoseconds = 0;
	size_t i;

	if (target->phony || !target->noutputs)
		return 1;
	for (i = 0; i < target->ninputs; ++i) {
		long seconds, nanoseconds;
		if (file_mtime(target->inputs[i], &seconds, &nanoseconds) < 0)
			return 1;
		newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
	}
	for (i = 0; i < target->ndeps; ++i) {
		struct target *dependency = find_target(target->deps[i]);
		if (dependency) {
			for (size_t j = 0; j < dependency->noutputs; ++j) {
				long seconds, nanoseconds;
				if (file_mtime(dependency->outputs[j], &seconds, &nanoseconds) < 0)
					return 1;
				newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
			}
		} else {
			long seconds, nanoseconds;
			if (file_mtime(target->deps[i], &seconds, &nanoseconds) < 0)
				return 1;
			newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
		}
	}
	for (i = 0; i < target->noutputs; ++i) {
		long seconds, nanoseconds;
		if (file_mtime(target->outputs[i], &seconds, &nanoseconds) < 0)
			return 1;
		if (!i || seconds < oldest_seconds ||
		 (seconds == oldest_seconds && nanoseconds < oldest_nanoseconds)) {
			oldest_seconds = seconds;
			oldest_nanoseconds = nanoseconds;
		}
	}
	return newest_seconds > oldest_seconds ||
	 (newest_seconds == oldest_seconds && newest_nanoseconds > oldest_nanoseconds);
}

static int
append_text(char *buffer, size_t size, size_t *length, const char *text)
{
	size_t text_length = strlen(text);

	if (*length + text_length + 1 > size)
		return -1;
	memcpy(buffer + *length, text, text_length);
	*length += text_length;
	buffer[*length] = 0;
	return 0;
}

/* Expand the three Make-compatible automatic variables in a command.  Shell
 * variables remain untouched; `$$` escapes a literal dollar for the shell. */
static int
expand_command(struct target *target, const char *command, char *buffer, size_t size)
{
	size_t length = 0;
	const char *first = target->ndeps ? target->deps[0] :
		target->ninputs ? target->inputs[0] : "";

	buffer[0] = 0;
	while (*command) {
		if (*command != '$' || !command[1]) {
			char character[2] = { *command++, 0 };
			if (append_text(buffer, size, &length, character) < 0)
				return -1;
			continue;
		}
		++command;
		if (*command == '@') {
			if (append_text(buffer, size, &length, target->name) < 0)
				return -1;
		} else if (*command == '<') {
			if (append_text(buffer, size, &length, first) < 0)
				return -1;
		} else if (*command == '^') {
			for (size_t i = 0; i < target->ndeps + target->ninputs; ++i) {
				const char *item = i < target->ndeps ? target->deps[i] :
					target->inputs[i - target->ndeps];
				if (i && append_text(buffer, size, &length, " ") < 0)
					return -1;
				if (append_text(buffer, size, &length, item) < 0)
					return -1;
			}
		} else if (*command == '*') {
			if (append_text(buffer, size, &length, target->stem ? target->stem : "") < 0)
				return -1;
		} else if (*command == '$') {
			if (append_text(buffer, size, &length, "$") < 0)
				return -1;
		} else {
			char characters[3] = { '$', *command, 0 };
			if (append_text(buffer, size, &length, characters) < 0)
				return -1;
		}
		++command;
	}
	return 0;
}

