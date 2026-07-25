/* meow - YAML-ish recipe parser and target-table management.
 *
 * parse_recipe() is the public entry; add_target/find_target/
 * append_environment/add_list/add_dependencies/add_command are the
 * helpers it drives. find_target() is also called from graph.c and
 * main.c, so it is non-static; the rest stay file-local. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meow.h"

static int
indent_of(const char *line)
{
	int indent = 0;
	while (*line == ' ') {
		++line;
		++indent;
	}
	return indent;
}

static char *
trim(char *text)
{
	char *end;
	while (*text == ' ' || *text == '\t')
		++text;
	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = 0;
	return text;
}

struct target *
find_target(const char *name)
{
	for (size_t i = 0; i < ntargets; ++i)
		if (strcmp(targets[i].name, name) == 0)
			return &targets[i];
	for (size_t i = 0; i < ntargets; ++i) {
		struct target *pattern = &targets[i];
		char *marker = strchr(pattern->name, '%');
		size_t prefix_length;
		size_t suffix_length;
		size_t name_length;
		struct target *instance;
		char *stem;

		if (!marker || strchr(marker + 1, '%'))
			continue;
		prefix_length = (size_t)(marker - pattern->name);
		suffix_length = strlen(marker + 1);
		name_length = strlen(name);
		if (name_length < prefix_length + suffix_length
		 || memcmp(name, pattern->name, prefix_length) != 0
		 || strcmp(name + name_length - suffix_length, marker + 1) != 0)
			continue;
		if (ntargets == TARGET_MAX)
			return 0;
		stem = malloc(name_length - prefix_length - suffix_length + 1);
		if (!stem)
			return 0;
		memcpy(stem, name + prefix_length, name_length - prefix_length - suffix_length);
		stem[name_length - prefix_length - suffix_length] = 0;
		instance = &targets[ntargets++];
		*instance = *pattern;
		instance->name = strdup(name);
		instance->stem = stem;
		instance->visiting = 0;
		instance->done = 0;
		for (size_t j = 0; j < instance->ndeps; ++j) {
			char *percent = strchr(instance->deps[j], '%');
			if (percent) {
				size_t before = (size_t)(percent - instance->deps[j]);
				char *expanded = malloc(before + strlen(stem) + strlen(percent + 1) + 1);
				if (!expanded) return 0;
				memcpy(expanded, instance->deps[j], before);
				strcpy(expanded + before, stem);
				strcpy(expanded + before + strlen(stem), percent + 1);
				instance->deps[j] = expanded;
			}
		}
		for (size_t j = 0; j < instance->ninputs + instance->noutputs; ++j) {
			char **item = j < instance->ninputs ? &instance->inputs[j] :
				&instance->outputs[j - instance->ninputs];
			char *percent = strchr(*item, '%');
			if (percent) {
				size_t before = (size_t)(percent - *item);
				char *expanded = malloc(before + strlen(stem) + strlen(percent + 1) + 1);
				if (!expanded) return 0;
				memcpy(expanded, *item, before);
				strcpy(expanded + before, stem);
				strcpy(expanded + before + strlen(stem), percent + 1);
				*item = expanded;
			}
		}
		return instance;
	}
	return 0;
}

static struct target *
add_target(char *name)
{
	if (!*name || ntargets == TARGET_MAX)
		return 0;
	targets[ntargets].name = name;
	return &targets[ntargets++];
}

static int
append_environment(char *key, char *value)
{
	size_t key_length = strlen(key);
	size_t length = strlen(recipe_environment);

	if (!key_length)
		return -1;
	for (size_t i = 0; i < key_length; ++i)
		if (!((key[i] >= 'A' && key[i] <= 'Z') ||
		      (key[i] >= 'a' && key[i] <= 'z') || key[i] == '_' ||
		      (i && key[i] >= '0' && key[i] <= '9')))
			return -1;
	if (length + 8 + key_length + 2 * strlen(value) + 4 >= sizeof(recipe_environment))
		return -1;
	memcpy(recipe_environment + length, "export ", 7);
	length += 7;
	memcpy(recipe_environment + length, key, key_length);
	length += key_length;
	recipe_environment[length++] = '=';
	/* use single quotes; if value contains ', use '\\'' escape trick */
	recipe_environment[length++] = '\'';
	while (*value) {
		if (*value == '\'') {
			recipe_environment[length++] = '\\';
			recipe_environment[length++] = '\'';
			recipe_environment[length++] = '\'';
			if (value[1]) {
				recipe_environment[length++] = '\'';
			}
		} else {
			recipe_environment[length++] = *value;
		}
		++value;
	}
	recipe_environment[length++] = '\'';
	recipe_environment[length++] = ';';
	recipe_environment[length++] = ' ';
	recipe_environment[length] = 0;
	return 0;
}

static int
add_list(char **items, size_t *count, size_t maximum, char *value)
{
	char *item;
	if (*value == '[') {
		char *end = strrchr(value, ']');
		if (!end)
			return -1;
		*end = 0;
		++value;
	}
	item = value;
	while (item && *item) {
		char *next = strchr(item, ',');
		if (next)
			*next++ = 0;
		item = trim(item);
		if (*item && (*count == maximum))
			return -1;
		if (*item)
			items[(*count)++] = item;
		item = next;
	}
	return 0;
}

static int
add_dependencies(struct target *target, char *value)
{
	return add_list(target->deps, &target->ndeps, TARGET_DEPS_MAX, value);
}

static int
add_command(struct target *target, char *command)
{
	if (!target || !*command || target->ncommands == TARGET_COMMANDS_MAX)
		return -1;
	target->commands[target->ncommands++] = command;
	return 0;
}

/* The supported native format is deliberately Makefile-shaped: variables and
 * named targets, each with dependencies and commands.  "env", "steps", and
 * "run" remain accepted solely for migration from the original runner. */
int
parse_recipe(char *data)
{
	enum { ROOT, PROBE, PROBE_HEADERS, PROBE_FUNCS, VARIABLES, TARGETS, COMMANDS } section = ROOT;
	struct target *current = 0;
	int probe_section = 0;  /* 0=none, 1=keyval, 2=list */
	char *line = data;

	recipe_environment[0] = 0;
	ntargets = 0;
	default_target = 0;
	probe_reset();
	while (*line) {
		char *end = line;
		char *text;
		int indent;
		while (*end && *end != '\n')
			++end;
		if (*end)
			*end++ = 0;
		indent = indent_of(line);
		text = trim(line + indent);
		if (!*text || *text == '#') {
			line = end;
			continue;
		}
		if (indent == 0) {
			section = ROOT;
			current = 0;
			probe_section = 0;
			if (strcmp(text, "probe:") == 0)
				section = PROBE;
			else if (strcmp(text, "variables:") == 0 || strcmp(text, "env:") == 0)
				section = VARIABLES;
			else if (strcmp(text, "targets:") == 0 || strcmp(text, "steps:") == 0)
				section = TARGETS;
			else if (strncmp(text, "default:", 8) == 0)
				default_target = trim(text + 8);
		} else if ((section == PROBE || section == PROBE_HEADERS || section == PROBE_FUNCS) && indent == 2) {
			section = PROBE;
			if (strcmp(text, "headers:") == 0) {
				section = PROBE_HEADERS;
			} else if (strcmp(text, "functions:") == 0) {
				section = PROBE_FUNCS;
			} else {
				/* key: value pairs */
				char *colon = strchr(text, ':');
				if (!colon) return -1;
				*colon = 0;
				char *key = trim(text);
				char *val = trim(colon + 1);
				if (strcmp(key, "cc") == 0) probe_set_cc(val);
				else if (strcmp(key, "cflags") == 0) probe_set_cflags(val);
				else if (strcmp(key, "config") == 0) probe_set_config(val);
			}
		} else if ((section == PROBE_HEADERS || section == PROBE_FUNCS) && indent == 4 && *text == '-') {
			if (section == PROBE_HEADERS)
				probe_add_header(trim(text + 1));
			else
				probe_add_function(trim(text + 1));
		} else if (section == PROBE_FUNCS && indent == 4 && *text == '-') {
			probe_add_function(trim(text + 1));
		} else if (section == PROBE && indent > 2) {
			/* skip unknown sub-items */
		} else if (section == VARIABLES && indent == 2) {
			char *colon = strchr(text, ':');
			if (!colon)
				return -1;
			*colon = 0;
			if (append_environment(trim(text), trim(colon + 1)) != 0)
				return -1;
		} else if ((section == TARGETS || section == COMMANDS) && indent == 2) {
			char *colon = strchr(text, ':');
			if (!colon || colon[1])
				return -1;
			*colon = 0;
			current = add_target(trim(text));
			if (!current)
				return -1;
			section = TARGETS;
		} else if (section == TARGETS && indent == 4 && current) {
			if (strncmp(text, "deps:", 5) == 0) {
				if (add_dependencies(current, trim(text + 5)) != 0)
					return -1;
			} else if (strncmp(text, "inputs:", 7) == 0) {
				if (add_list(current->inputs, &current->ninputs,
					TARGET_DEPS_MAX, trim(text + 7)) != 0)
					return -1;
			} else if (strncmp(text, "outputs:", 8) == 0) {
				if (add_list(current->outputs, &current->noutputs,
					TARGET_DEPS_MAX, trim(text + 8)) != 0)
					return -1;
			} else if (strcmp(text, "phony: true") == 0) {
				current->phony = 1;
			} else if (strcmp(text, "commands:") == 0) {
				section = COMMANDS;
			} else if (strncmp(text, "run:", 4) == 0) {
				if (add_command(current, trim(text + 4)) != 0)
					return -1;
			} else {
				return -1;
			}
		} else if (section == COMMANDS && indent >= 6 && current && text[0] == '-' && text[1] == ' ') {
			if (add_command(current, trim(text + 2)) != 0)
				return -1;
		} else {
			return -1;
		}
		line = end;
	}
	return ntargets ? 0 : -1;
}

