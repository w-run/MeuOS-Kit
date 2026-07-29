/* pkg_config.c — meow pkg-config 子命令
 *
 * 提供与标准 pkg-config 兼容的 CLI 接口，依赖内置已知库数据库
 * (pkglib.c) 而非 .pc 文件。支持 --cflags / --libs 等常见选项。
 *
 * 当 autoconf/meson 等构建系统调用 `pkg-config` 时，
 * meow 的 pkg-config 符号链接（或别名）将透明接管查询。 */

#include <stdio.h>
#include <string.h>
#include "meow.h"

int
cmd_pkg_config(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "usage: meow pkg-config [--cflags|--libs] <library> ...\n");
		return 1;
	}

	int want_cflags = 0;
	int want_libs = 1;  /* default: --libs */
	int first_pkg = 0;

	/* Parse flags */
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--cflags") == 0) {
			want_cflags = 1;
			want_libs = 0;
		} else if (strcmp(argv[i], "--libs") == 0) {
			want_libs = 1;
		} else if (strcmp(argv[i], "--cflags-only-I") == 0) {
			want_cflags = 1;
		} else if (strcmp(argv[i], "--cflags-only-other") == 0) {
			want_cflags = 1;
		} else if (strcmp(argv[i], "--static") == 0) {
			/* ignored, static by default */
		} else if (strcmp(argv[i], "--exists") == 0) {
			/* --exists: just check if package is known */
			continue;
		} else {
			first_pkg = i;
			break;
		}
	}

	if (first_pkg >= argc) {
		/* List all known packages */
		fprintf(stderr, "known packages:");
		for (int i = 0; known_libs[i].name; i++)
			fprintf(stderr, " %s", known_libs[i].name);
		fprintf(stderr, "\n");
		return 1;
	}

	int found = 0;
	for (int pi = first_pkg; pi < argc; pi++) {
		const char *pkg_name = argv[pi];
		int matched = 0;

		for (int i = 0; known_libs[i].name; i++) {
			if (strcmp(known_libs[i].name, pkg_name) == 0) {
				if (found > 0) putchar(' ');
				if (want_cflags && known_libs[i].cflags[0])
					printf("%s", known_libs[i].cflags);
				if (want_libs && known_libs[i].libs[0])
					printf("%s", known_libs[i].libs);
				found++;
				matched = 1;
				break;
			}
		}

		if (!matched)
			fprintf(stderr, "meow pkg-config: unknown package '%s'\n", pkg_name);
	}

	if (found > 0) putchar('\n');
	return found > 0 ? 0 : 1;
}
