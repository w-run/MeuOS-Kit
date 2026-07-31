#include <libgen.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
	char path[64];

	strcpy(path, "/usr/bin");
	if (strcmp(dirname(path), "/usr") != 0)
		return 1;
	strcpy(path, "/usr/bin/");
	if (strcmp(dirname(path), "/usr") != 0)
		return 1;
	strcpy(path, "/");
	if (strcmp(dirname(path), "/") != 0)
		return 1;
	strcpy(path, "a/b/c");
	if (strcmp(dirname(path), "a/b") != 0)
		return 1;
	strcpy(path, "a");
	if (strcmp(dirname(path), ".") != 0)
		return 1;

	strcpy(path, "/usr/bin");
	if (strcmp(basename(path), "bin") != 0)
		return 1;
	strcpy(path, "/usr/bin/");
	if (strcmp(basename(path), "bin") != 0)
		return 1;
	strcpy(path, "/");
	if (strcmp(basename(path), "/") != 0)
		return 1;
	strcpy(path, "a/b");
	if (strcmp(basename(path), "b") != 0)
		return 1;
	if (strcmp(basename(NULL), ".") != 0 || strcmp(dirname(NULL), ".") != 0)
		return 1;

	puts("PASS libgen");
	return 0;
}
