/* pkg_config.c — meow pkg-config 子命令 + 库查询 API
 *
 * 提供与标准 pkg-config 兼容的 CLI 接口，依赖内置已知库数据库
 * (pkglib.c) 而非 .pc 文件。支持 --cflags / --libs 等常见选项。
 *
 * 当 autoconf/meson 等构建系统调用 `pkg-config` 时，
 * meow 的 pkg-config 符号链接（或别名）将透明接管查询。
 *
 * 同时导出 lookup_lib_*() API 供配方系统在 uses: 字段中使用。 */

#include <stdio.h>
#include <string.h>
#include "meow.h"

/* 查询库的编译标志。返回字符串指针或 NULL。 */
const char *
lookup_lib_cflags(const char *name)
{
	const struct pkg_lib *lib = find_lib(name);
	return lib ? lib->cflags : NULL;
}

/* 查询库的链接标志。返回字符串指针或 NULL。 */
const char *
lookup_lib_libs(const char *name)
{
	const struct pkg_lib *lib = find_lib(name);
	return lib ? lib->libs : NULL;
}

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
		const struct pkg_lib *lib = find_lib(pkg_name);
		if (lib) {
			if (found > 0) putchar(' ');
			if (want_cflags && lib->cflags[0])
				fputs(lib->cflags, stdout);
			if (want_libs && lib->libs[0])
				fputs(lib->libs, stdout);
			found++;
		} else {
			fprintf(stderr, "meow pkg-config: unknown package '%s'\n", pkg_name);
		}
	}

	if (found > 0) putchar('\n');
	return found > 0 ? 0 : 1;
}
