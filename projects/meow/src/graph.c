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

/* Forward declaration of expand_env_vars from parse.c */

/* Progress counters (declared extern in meow.h; defined here). */
int total_commands = 0;
int completed_commands = 0;

/* Expand ${VAR} in path, using a stack buffer for the result.
 * Returns the original path if no expansion is needed, or a pointer
 * to a static buffer with the expanded result. */
static const char *
expand_path(const char *path)
{
	static char buffer[RECIPE_MAX];
	if (strchr(path, '$') && strchr(path, '{')) {
		if (expand_env_vars(path, buffer, sizeof(buffer)) == 0)
			return buffer;
	}
	return path;
}

/* Evaluate a "when:" condition expression.
 * Returns 1 if condition is met (or no condition), 0 if skipped. */
static int
eval_condition(const char *when_expr)
{
	if (!when_expr || !*when_expr)
		return 1;

	/* Parse: EXPR OP "VALUE" */
	char expr[64], op[8], value[128];
	if (sscanf(when_expr, "%63[^ ] %7[^ ] \"%127[^\"]\"", expr, op, value) < 3)
		return 1;  /* unparseable = true (graceful fallback) */

	const char *actual = NULL;
	if (strcmp(expr, "ARCH") == 0)
		actual = build_arch;
	else if (strcmp(expr, "TARGET") == 0)
		actual = build_target;

	if (!actual)
		return 1;  /* unknown variable = true (graceful fallback) */

	if (strcmp(op, "==") == 0)
		return strcmp(actual, value) == 0;
	if (strcmp(op, "!=") == 0)
		return strcmp(actual, value) != 0;
	return 1;  /* unknown op = true */
}

/* File-local helpers (defined below; run_target() calls them). */
static int target_out_of_date(struct target *);
static int expand_command(struct target *, const char *, char *, size_t);

/* Draw a progress bar to stdout. Called after each command completes
 * during parallel builds. */
static void
show_progress(const char *current)
{
	if (g_output_mode == OUTPUT_QUIET || g_output_mode == OUTPUT_JSON)
		return;
	int pct = total_commands ? (completed_commands * 100 / total_commands) : 0;
	int width = 12;
	int filled = width * pct / 100;
	/* Clear the current line, then draw progress. */
	fprintf(stdout, "\r\033[K  [");
	for (int i = 0; i < width; i++)
		fprintf(stdout, "%s", i < filled ? "█" : "░");
	if (current)
		fprintf(stdout, "] %d%% (%d/%d) · %s", pct, completed_commands, total_commands, current);
	else
		fprintf(stdout, "] %d%% (%d/%d)", pct, completed_commands, total_commands);
	fflush(stdout);
	if (completed_commands >= total_commands)
		fprintf(stdout, "\n");
}

/* Count total commands across all targets once. */
static void
count_total_commands(void)
{
	if (total_commands > 0)
		return;
	for (size_t i = 0; i < ntargets; i++)
		total_commands += targets[i].ncommands;
}

int
run_target(struct target *target)
{
	if (!target || target->visiting)
		return -1;
	if (target->done)
		return 0;
	/* Skip if condition not met */
	if (target->when && !eval_condition(target->when)) {
		target->done = 1;
		return 0;
	}
	target->visiting = 1;
	if (parallel_jobs > 1) {
		pid_t children[TARGET_DEPS_MAX];
		struct target *child_targets[TARGET_DEPS_MAX];
		size_t active = 0;
		int failed = 0;
		count_total_commands();

		/* Collect unique direct dependencies — skip already-done and
		 * duplicate entries so the same target is never forked twice. */
		struct target *uniques[TARGET_DEPS_MAX];
		size_t nuniques = 0;
		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (!dependency || dependency->done)
				continue;
			int skip = 0;
			for (size_t j = 0; j < nuniques; j++)
				if (uniques[j] == dependency) { skip = 1; break; }
			if (!skip)
				uniques[nuniques++] = dependency;
		}

		for (size_t i = 0; i < nuniques; ++i) {
			struct target *dependency = uniques[i];
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
		/* Serial execution: dedup and skip already-done deps */
		struct target *uniques[TARGET_DEPS_MAX];
		size_t nuniques = 0;
		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (!dependency || dependency->done)
				continue;
			int skip = 0;
			for (size_t j = 0; j < nuniques; j++)
				if (uniques[j] == dependency) { skip = 1; break; }
			if (!skip)
				uniques[nuniques++] = dependency;
		}
		for (size_t i = 0; i < nuniques; ++i) {
			if (run_target(uniques[i]) != 0)
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
				completed_commands++;
				if (parallel_jobs > 1)
					show_progress(target->name);
			}
	else {
		/* Target is up-to-date, still count for progress. */
		completed_commands += target->ncommands;
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
		if (file_mtime(expand_path(target->inputs[i]), &seconds, &nanoseconds) < 0)
			return 1;
		newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
	}
	for (i = 0; i < target->ndeps; ++i) {
		struct target *dependency = find_target(target->deps[i]);
		if (dependency) {
			for (size_t j = 0; j < dependency->noutputs; ++j) {
				long seconds, nanoseconds;
				if (file_mtime(expand_path(dependency->outputs[j]), &seconds, &nanoseconds) < 0)
					return 1;
				newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
			}
		} else {
			long seconds, nanoseconds;
			if (file_mtime(expand_path(target->deps[i]), &seconds, &nanoseconds) < 0)
				return 1;
			newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
		}
	}
	for (i = 0; i < target->noutputs; ++i) {
		long seconds, nanoseconds;
		if (file_mtime(expand_path(target->outputs[i]), &seconds, &nanoseconds) < 0)
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

