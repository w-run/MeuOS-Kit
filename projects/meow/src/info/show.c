/* meow show — 配方预览/信息展示
 *
 * meow show <pkg>  显示配方的完整信息（target/deps/vars）
 */
#include "meow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* load_recipe is in recipe.c and declared in meow.h via include */

int
cmd_show(int argc, char **argv)
{
	/* Show currently loaded recipe info.
	 * The recipe must have been loaded before calling this. */
	if (argc < 1) {
		fprintf(stderr, "usage: meow show <package>\n");
		return 2;
	}
	/* Load the recipe */
	char data[RECIPE_MAX];
	char path[512];
	if (load_recipe(argv[0], path, sizeof(path), data) < 0) {
		fprintf(stderr, "meow: cannot load recipe for '%s'\n", argv[0]);
		return 1;
	}
	/* Parse and set up environment */
	size_t plen = strlen(path);
	int is_meow = (plen >= 5 && strcmp(path + plen - 5, ".meow") == 0);
	if (is_meow) {
		if (parse_meow(data) != 0) {
			fprintf(stderr, "meow: invalid .meow recipe\n");
			return 1;
		}
	} else {
		if (parse_recipe(data) != 0) {
			fprintf(stderr, "meow: invalid recipe\n");
			return 1;
		}
	}

	printf("=== 配方信息 ===\n");

	/* Show environment variables */
	const char *vars[] = {"NAME", "VERSION", "CC", "CFLAGS",
	                      "TARGET_TRIPLE", "ARCH", "PKGDIR",
	                      "BUILDDIR", "WORKDIR", NULL};
	for (int i = 0; vars[i]; i++) {
		const char *val = getenv(vars[i]);
		if (val)
			printf("  %s: %s\n", vars[i], val);
	}

	/* Show meta info */
	const char *meta_desc = getenv("description");
	const char *meta_license = getenv("license");
	const char *meta_author = getenv("author");
	const char *meta_homepage = getenv("homepage");
	if (meta_desc || meta_license || meta_author || meta_homepage) {
		printf("\n--- 元数据 ---\n");
		if (meta_desc)    printf("  description: %s\n", meta_desc);
		if (meta_license) printf("  license: %s\n", meta_license);
		if (meta_author)  printf("  author: %s\n", meta_author);
		if (meta_homepage) printf("  homepage: %s\n", meta_homepage);
	}

	/* Show targets */
	printf("\n--- 构建目标 ---\n");
	if (ntargets == 0) {
		printf("  (无)\n");
	} else {
		for (size_t i = 0; i < ntargets; i++) {
			struct target *t = &targets[i];
			printf("  %s%s%s", t->name,
			       (default_target && strcmp(t->name, default_target) == 0)
			       ? " [default]" : "",
			       t->ncommands > 0 ? "" : " (no commands)");
			printf("\n");
			if (t->ndeps > 0) {
				printf("    deps:");
				for (size_t j = 0; j < t->ndeps; j++)
					printf(" %s", t->deps[j]);
				printf("\n");
			}
			if (t->ncommands > 0 && t->commands[0]) {
				/* Show first line of first command */
				const char *cmd = t->commands[0];
				char buf[80];
				size_t flen = strlen(cmd);
				size_t show = flen < 60 ? flen : 60;
				memcpy(buf, cmd, show);
				buf[show] = '\0';
				char *nl = strchr(buf, '\n');
				if (nl) *nl = '\0';
				printf("    run: %s\n", buf);
			}
		}
	}
	return 0;
}
