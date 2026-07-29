/* meow init — 自动生成 .meow 配方
 *
 * meow init           扫描当前目录，生成 meow.meow
 * meow init <pkg>     为 pkgs/<pkg> 生成 meow.meow
 *
 * 自动检测：
 *   - 是否有 Makefile/configure → 推导构建方式
 *   - 是否有 *.c/*.h → 推导语言
 *   - 项目名（从目录名）
 */
#include "meow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int
file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static int
has_files_with_ext(const char *dir, const char *ext)
{
	DIR *d = opendir(dir);
	if (!d) return 0;
	struct dirent *e;
	int found = 0;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		if (len > strlen(ext) && strcmp(e->d_name + len - strlen(ext), ext) == 0) {
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

static void
write_meow(FILE *f, const char *pkgname, const char *build_system)
{
	fprintf(f, "# %s — 自动生成的 MeuOS 构建配方\n", pkgname);
	fprintf(f, "name: %s\n", pkgname);
	fprintf(f, "version: 0.1.0\n");
	fprintf(f, "\n");

	/* 构建工具 */
	fprintf(f, "# 构建环境\n");
	fprintf(f, "CC: gcc\n");
	fprintf(f, "CFLAGS: -O2\n\n");

	/* 构建目标 */
	if (strcmp(build_system, "make") == 0) {
		fprintf(f, "[build]\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    make -j$(nproc)\n\n");
		fprintf(f, "[install]\n");
		fprintf(f, "  deps: build\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    make install DESTDIR=%%PKGDIR%%/install\n\n");
		fprintf(f, "[clean]\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    make clean\n");
	} else if (strcmp(build_system, "configure") == 0) {
		fprintf(f, "[configure]\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    ./configure --prefix=/usr\n\n");
		fprintf(f, "[build]\n");
		fprintf(f, "  deps: configure\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    make -j$(nproc)\n\n");
		fprintf(f, "[install]\n");
		fprintf(f, "  deps: build\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    make install DESTDIR=%%PKGDIR%%/install\n");
	} else {
		/* 简单编译 */
		fprintf(f, "[build]\n");
		fprintf(f, "  run:\n");
		/* 找 .c 文件生成编译命令 */
		DIR *d = opendir(".");
		if (d) {
			struct dirent *e;
			while ((e = readdir(d))) {
				size_t len = strlen(e->d_name);
				if (len > 2 && strcmp(e->d_name + len - 2, ".c") == 0) {
					char outname[256];
					snprintf(outname, sizeof(outname), "%s", e->d_name);
					outname[len - 2] = '\0'; /* remove .c */
					fprintf(f, "    %%CC%% %%CFLAGS%% -o %s %s\n", outname, e->d_name);
				}
			}
			closedir(d);
		}
		fprintf(f, "\n");
		fprintf(f, "[clean]\n");
		fprintf(f, "  run:\n");
		fprintf(f, "    rm -f *.o\n");
	}

	fprintf(f, "\n");
	fprintf(f, "default: build\n");
}

int
cmd_init(int argc, char **argv)
{
	const char *pkgname;
	char target_dir[1024] = "";

	if (argc >= 1) {
		pkgname = argv[0];
		snprintf(target_dir, sizeof(target_dir), "pkgs/%s", pkgname);
		/* Create package directory if needed */
		if (access(target_dir, F_OK) != 0) {
			if (mkdir(target_dir, 0755) != 0) {
				meow_msg(MSG_ERROR, "cannot create %s", target_dir);
				return 1;
			}
		}
	} else {
		/* 用当前目录名作为包名 */
		char cwd[1024];
		if (!getcwd(cwd, sizeof(cwd))) return 1;
		const char *last_slash = strrchr(cwd, '/');
		pkgname = last_slash ? last_slash + 1 : cwd;
		snprintf(target_dir, sizeof(target_dir), ".");

		/* 检查是否已有 meow.meow */
		char existing[1024];
		snprintf(existing, sizeof(existing), "%s/meow.meow", target_dir);
		if (file_exists(existing)) {
			meow_msg(MSG_ERROR, "meow.meow already exists");
			return 1;
		}
	}

	char outpath[1024];
	snprintf(outpath, sizeof(outpath), "%s/meow.meow", target_dir);

	/* 检测构建系统 */
	const char *build_system = "simple";
	if (file_exists("Makefile") || file_exists("GNUmakefile"))
		build_system = "make";
	else if (file_exists("configure") || file_exists("configure.ac"))
		build_system = "configure";
	else if (has_files_with_ext(".", ".c"))
		build_system = "simple";
	else
		build_system = "simple";

	FILE *f = fopen(outpath, "w");
	if (!f) {
		meow_msg(MSG_ERROR, "cannot write %s", outpath);
		return 1;
	}

	/* Change to target dir for source detection */
	char orig_cwd[1024];
	getcwd(orig_cwd, sizeof(orig_cwd));
	if (target_dir[0] != '.') chdir(target_dir);

	write_meow(f, pkgname, build_system);
	fclose(f);

	if (target_dir[0] != '.') chdir(orig_cwd);

	meow_msg(MSG_SUCCESS, "created %s", outpath);
	return 0;
}
