/* meow - entry point, argument dispatch, and Makefile/bootstrap
 * compatibility paths. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meow.h"

int
main(int argc, char **argv)
{
	char *data;
	char path[512];
	char *requested;
	char **arguments = argv + 1;
	int count = argc - 1;

	if (count && strncmp(arguments[0], "-j", 2) == 0) {
		const char *value = arguments[0] + 2;
		if (!*value) {
			if (count < 2)
				return 2;
			value = arguments[1];
			++arguments;
			--count;
		}
		parallel_jobs = atoi(value);
		if (parallel_jobs < 1)
			return 2;
		++arguments;
		--count;
	}

	if (count == 1 && strcmp(arguments[0], "list") == 0)
		return list_packages() == 0 ? 0 : 1;
	if (count == 1 && strcmp(arguments[0], "--bootstrap") == 0) {
		if (run("CC=\"${CC:-cc}\" make -C meow clean all") != 0)
			return 1;
		printf("meow: bootstrap build complete\n");
		return 0;
	}
	if ((count != 2 && count != 3) ||
	    (strcmp(arguments[0], "build") != 0 && strcmp(arguments[0], "clean") != 0)) {
		printf("usage: meow [-jN] build <package> [target] | meow [-jN] clean <package> | meow list | meow --bootstrap\n");
		return 2;
	}
	data = malloc(RECIPE_MAX);
	if (!data) {
		printf("meow: out of memory\n");
		return 1;
	}
	if (load_recipe(arguments[1], path, sizeof(path), data) < 0 && strcmp(arguments[1], ".") == 0 && strcmp(arguments[0], "build") == 0) {
		if (run("CC=mcc DESTDIR=\"${MEUOS_SYSROOT:-./sysroot}\" PREFIX=/usr make") != 0) {
			printf("meow: Makefile compatibility build failed\n");
			return 1;
		}
		printf("meow: built current Makefile (compatibility mode)\n");
		return 0;
	}
	if (load_recipe(arguments[1], path, sizeof(path), data) < 0 || parse_recipe(data) != 0) {
		printf("meow: invalid or unreadable build file for %s\n", arguments[1]);
		return 1;
	}
	requested = strcmp(arguments[0], "clean") == 0 ? "clean" : count == 3 ? arguments[2] : default_target;
	if (!requested)
		requested = find_target("all") ? "all" : "build";
	if (run_target(find_target(requested)) != 0) {
		printf("meow: target failed: %s (%s)\n", requested, path);
		return 1;
	}
	printf("meow: %s %s%s%s\n", strcmp(arguments[0], "clean") == 0 ? "cleaned" : "built",
	       arguments[1], count == 3 ? " target " : "", count == 3 ? arguments[2] : "");
	return 0;
}
