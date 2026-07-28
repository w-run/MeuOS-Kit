/* meow - entry point, argument dispatch, and Makefile/bootstrap
 * compatibility paths. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meow.h"

static void
set_arch_env(void)
{
	/* Inject ARCH and HOST into recipe_environment so recipes can use
	 * $(ARCH) for cross-compiler dispatch and $(HOST) for autoconf-style
	 * --host triplets. */
	size_t len = strlen(recipe_environment);
	const char *arch = build_arch;
	const char *host = NULL;

	if (!arch) {
		/* Auto-detect from kernel. */
		struct utsname uts;
		if (uname(&uts) == 0) {
			const char *m = uts.machine;
			if (strcmp(m, "x86_64") == 0) arch = "x86_64";
			else if (strcmp(m, "aarch64") == 0) arch = "aarch64";
			else if (strcmp(m, "riscv64") == 0) arch = "riscv64";
			else if (strcmp(m, "loongarch64") == 0) arch = "loongarch64";
			else if (strncmp(m, "i386", 4) == 0 ||
			         strcmp(m, "i486") == 0 ||
			         strcmp(m, "i586") == 0 ||
			         strcmp(m, "i686") == 0) arch = "i386";
		}
	}
	if (!arch)
		arch = "x86_64";

	/* Derive autoconf HOST triplet from ARCH. */
	if      (strcmp(arch, "x86_64")      == 0) host = "x86_64-unknown-linux-gnu";
	else if (strcmp(arch, "aarch64")     == 0) host = "aarch64-unknown-linux-gnu";
	else if (strcmp(arch, "riscv64")     == 0) host = "riscv64-unknown-linux-gnu";
	else if (strcmp(arch, "loongarch64") == 0) host = "loongarch64-unknown-linux-gnu";
	else if (strcmp(arch, "i386")        == 0) host = "i686-unknown-linux-gnu";

	if (len + 64 < sizeof(recipe_environment)) {
		size_t n = snprintf(recipe_environment + len,
		                    sizeof(recipe_environment) - len,
		                    "export ARCH='%s'; export HOST='%s'; ",
		                    arch, host ? host : "x86_64-unknown-linux-gnu");
		if (n > 0)
			recipe_environment[len + n] = '\0';
	}
}

int
main(int argc, char **argv)
{
	char *data;
	char path[512];
	char *requested;
	char **arguments = argv + 1;
	int count = argc - 1;

	while (count > 0) {
		if (strncmp(arguments[0], "-j", 2) == 0) {
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
		} else if (strcmp(arguments[0], "--arch") == 0 && count >= 2) {
			build_arch = arguments[1];
			arguments += 2;
			count -= 2;
		} else if (strncmp(arguments[0], "--arch=", 7) == 0) {
			build_arch = arguments[0] + 7;
			++arguments;
			--count;
		} else {
			break;
		}
	}

	if (count == 1 && strcmp(arguments[0], "list") == 0)
		return list_packages() == 0 ? 0 : 1;
	if (count == 1 && strcmp(arguments[0], "--bootstrap") == 0) {
		if (run("CC=\"${CC:-cc}\" make -C meow clean all") != 0)
			return 1;
		printf("meow: bootstrap build complete\n");
		return 0;
	}
	if (count == 1 && strcmp(arguments[0], "--help") == 0) {
		printf("usage: meow [-jN] [--arch <arch>] build <package> [target]\n");
		printf("       meow [-jN] [--arch <arch>] build all\n");
		printf("       meow [-jN] [--arch <arch>] clean <package>\n");
		printf("       meow list\n");
		printf("       meow --bootstrap\n");
		printf("       meow --help\n");
		return 0;
	}
	/* Build all packages: scan pkgs/ and build each one */
	if (count == 2 && strcmp(arguments[0], "build") == 0 &&
	    strcmp(arguments[1], "all") == 0) {
		DIR *dir = opendir("pkgs");
		if (!dir) return 1;
		struct dirent *entry;
		int failures = 0;
		while ((entry = readdir(dir))) {
			if (entry->d_name[0] == '.') continue;
			printf("meow: building %s\n", entry->d_name);
			char cmd[1024];
			snprintf(cmd, sizeof(cmd), "meow build %s", entry->d_name);
			if (run(cmd) != 0) {
				printf("meow: %s failed\n", entry->d_name);
				failures++;
			}
		}
		closedir(dir);
		printf("meow: built all packages (%d failures)\n", failures);
		return failures ? 1 : 0;
	}
	if ((count != 2 && count != 3) ||
	    (strcmp(arguments[0], "build") != 0 && strcmp(arguments[0], "clean") != 0)) {
		printf("usage: meow [-jN] [--arch <arch>] build <package> [target] | meow [-jN] clean <package> | meow list | meow --bootstrap\n");
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
	/* Inject ARCH into recipe environment after parse. */
	set_arch_env();

	/* Run inline feature detection (probe section) if any probes are
	 * registered.  The generated config.h is placed in the current
	 * directory; recipes reference it via -I. or $PWD. */
	{	char *build_dir = getenv("BLD_DIR");
		if (probe_run(build_dir ? build_dir : ".") != 0) {
			printf("meow: probe failed for %s\n", arguments[1]);
			return 1;
		}
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
