/* meow - recipe file loading and root-level include resolution.
 *
 * The only cross-file entry point is load_recipe(); indent_of() and trim()
 * are file-local helpers reused only within this translation unit. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "meow.h"

int
load_recipe(const char *package, char *path, size_t path_size, char *data)
{
	const char prefix[] = "pkgs/";
	const char suffix[] = "/meow.meow";
	int descriptor;
	ssize_t count;

	/* If package starts with '/', treat as a direct path.
	 * If package is '.' (current directory), use ./meow.yaml. */
	if (package[0] == '/') {
		if (strlen(package) + 1 > path_size)
			return -1;
		strcpy(path, package);
	} else if (strcmp(package, ".") == 0) {
		/* Try .meow first, then .yaml */
		const char *local = "./meow.meow";
		if (strlen(local) + 1 > path_size)
			return -1;
		strcpy(path, local);
		descriptor = open(path, O_RDONLY);
		if (descriptor < 0) {
			local = "./meow.yaml";
			strcpy(path, local);
			descriptor = open(path, O_RDONLY);
			if (descriptor < 0) {
				local = "./meow.yml";
				strcpy(path, local);
				descriptor = open(path, O_RDONLY);
			}
		}
		if (descriptor < 0)
			return -1;
	} else {
		/* pkgs/<pkg>/meow.meow or meow.yaml */
		snprintf(path, path_size, "%s%s%s", prefix, package, suffix);
		descriptor = open(path, O_RDONLY);
		if (descriptor < 0) {
			snprintf(path, path_size, "%s%s/meow.yaml", prefix, package);
			descriptor = open(path, O_RDONLY);
		}
		if (descriptor < 0) {
			snprintf(path, path_size, "%s%s/meow.yml", prefix, package);
			descriptor = open(path, O_RDONLY);
		}
	}
	if (descriptor < 0)
		return -1;
	count = read(descriptor, data, RECIPE_MAX - 1);
	close(descriptor);
	if (count < 0)
		return -1;
	data[count] = 0;
	/* Root-level include files are deliberately a small convenience feature:
	 * each path is relative to the package recipe and is appended as another
	 * native YAML fragment.  Nested includes are intentionally deferred so
	 * recipe loading remains bounded and easy to audit. */
	for (size_t offset = 0, original = (size_t)count; offset < original;) {
		size_t start = offset;
		char *line;
		char *end;
		char include_path[512];
		char *directory;
		int include_fd;
		ssize_t include_count;
		size_t name_length;

		while (offset < original && data[offset] != '\n')
			++offset;
		line = data + start;
		end = data + offset;
		if (offset < original)
			++offset;
		if ((size_t)(end - line) < 8 || memcmp(line, "include:", 8) != 0)
			continue;
		line += 8;
		while (*line == ' ' || *line == '\t')
			++line;
		name_length = (size_t)(end - line);
		while (name_length && (line[name_length - 1] == ' ' || line[name_length - 1] == '\t'))
			--name_length;
		if (!name_length || name_length + 1 >= sizeof(include_path))
			return -1;
		directory = strrchr(path, '/');
		if (!directory)
			return -1;
		if ((size_t)(directory - path) + 1 + name_length + 1 > sizeof(include_path))
			return -1;
		memcpy(include_path, path, (size_t)(directory - path) + 1);
		memcpy(include_path + (directory - path) + 1, line, name_length);
		include_path[(directory - path) + 1 + name_length] = 0;
		include_fd = open(include_path, O_RDONLY);
		if (include_fd < 0)
			return -1;
		if ((size_t)count + 1 >= RECIPE_MAX) {
			close(include_fd);
			return -1;
		}
		data[count++] = '\n';
		include_count = read(include_fd, data + count, RECIPE_MAX - (size_t)count - 1);
		close(include_fd);
		if (include_count < 0)
			return -1;
		count += include_count;
		data[count] = 0;
	}
	return (int)count;
}
