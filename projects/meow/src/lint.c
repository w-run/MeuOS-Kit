/* meow lint - 配方语法检查器
 *
 * 检查 meow.yaml 配方的语法正确性。支持指定包名（meow lint <pkg>）
 * 或遍历当前目录/pkgs/ 下所有配方（meow lint 无参数）。
 */
#include "meow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* 检查一个配方文件。返回 0=通过，-1=失败。 */
static int
lint_recipe(const char *path)
{
	char data[RECIPE_MAX];
	char abs_path[512];
	if (load_recipe(path, abs_path, sizeof(abs_path), data) < 0) {
		meow_msg(MSG_ERROR, "%s: 无法加载配方", path);
		return -1;
	}
	if (parse_recipe(data) != 0) {
		meow_msg(MSG_ERROR, "%s: 语法错误", path);
		return -1;
	}
	meow_msg(MSG_SUCCESS, "✓ %s", path);
	return 0;
}

int
cmd_lint(int argc, char **argv)
{
	int npass = 0, nfail = 0;

	if (argc == 0) {
		/* 无参数：遍历 pkgs/ 和当前目录的 meow.yaml */
		/* 先看当前目录的 meow.yaml */
		FILE *f = fopen("meow.yaml", "r");
		if (f) {
			fclose(f);
			if (lint_recipe(".") == 0) npass++; else nfail++;
		}

		/* 再遍历 pkgs/ */
		DIR *d = opendir("pkgs");
		if (d) {
			struct dirent *entry;
			while ((entry = readdir(d))) {
				if (entry->d_name[0] == '.') continue;
				char pkgpath[256];
				snprintf(pkgpath, sizeof(pkgpath), "%s", entry->d_name);
				char path[512];
				char data[RECIPE_MAX];
				if (load_recipe(pkgpath, path, sizeof(path), data) == 0) {
					if (parse_recipe(data) == 0) {
						meow_msg(MSG_SUCCESS, "✓ %s", pkgpath);
						npass++;
					} else {
						meow_msg(MSG_ERROR, "%s: 语法错误", pkgpath);
						nfail++;
					}
				}
			}
			closedir(d);
		}
	} else {
		/* 指定包名 */
		for (int i = 0; i < argc; i++) {
			if (lint_recipe(argv[i]) == 0) npass++; else nfail++;
		}
	}

	if (nfail == 0)
		meow_msg(MSG_SUCCESS, "✓ %d 个配方检查通过", npass);
	else
		meow_msg(MSG_ERROR, "%d 通过, %d 失败", npass, nfail);
	return nfail ? 1 : 0;
}
