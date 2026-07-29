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
#include "triple.h"

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
		build_arch = (char *)arch;
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
		size_t n;
		if (build_target) {
			/* --target=<triple> was set: use full triple as HOST
			 * and export TARGET_TRIPLE for recipes to pass to mcc. */
			n = snprintf(recipe_environment + len,
			             sizeof(recipe_environment) - len,
			             "export ARCH='%s'; export HOST='%s'; "
			             "export TARGET_TRIPLE='%s'; ",
			             arch, build_target, build_target);
		} else {
			n = snprintf(recipe_environment + len,
			             sizeof(recipe_environment) - len,
			             "export ARCH='%s'; export HOST='%s'; ",
			             arch, host ? host : "x86_64-unknown-linux-gnu");
		}
		if (n > 0)
			recipe_environment[len + n] = '\0';
	}
}

int
main(int argc, char **argv)
{
	static const char *meow_self = NULL;  /* PATH to self, for recursive deps */
	meow_self = argv[0];
	char *data;
	char path[512];
	char *requested;
	char **arguments = argv + 1;
	int count = argc - 1;

	color_init();

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
			/* Don't consume a subcommand or another flag as arch value */
			if (arguments[1][0] == '-' ||
			    strcmp(arguments[1], "build") == 0 ||
			    strcmp(arguments[1], "clean") == 0 ||
			    strcmp(arguments[1], "list") == 0 ||
			    strcmp(arguments[1], "env") == 0 ||
			    strcmp(arguments[1], "lint") == 0 ||
			    strcmp(arguments[1], "show") == 0 ||
			    strcmp(arguments[1], "init") == 0 ||
			    strcmp(arguments[1], "init") == 0 ||
		    strcmp(arguments[1], "pkg-config") == 0 ||
		    strcmp(arguments[1], "import") == 0 ||
			    strcmp(arguments[1], "--bootstrap") == 0)
				return 2;
			build_arch = arguments[1];
			arguments += 2;
			count -= 2;
		} else if (strncmp(arguments[0], "--arch=", 7) == 0) {
			build_arch = arguments[0] + 7;
			++arguments;
			--count;
		} else if (strncmp(arguments[0], "--target=", 9) == 0) {
			/* 完整 triple：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
			 * 提取 canonical arch（支持别名如 amd64→x86_64）作为 build_arch，
			 * 完整 triple 存入 build_target 供 recipe 引用。 */
			const char *triple = arguments[0] + 9;
			build_target = triple;
			const char *arch_name = parse_triple_arch(triple);
			static char archbuf[64];
			snprintf(archbuf, sizeof(archbuf), "%s",
			         arch_name ? arch_name : triple);
			build_arch = archbuf;
			++arguments;
			--count;
		} else if (strcmp(arguments[0], "--quiet") == 0) {
			g_output_mode = OUTPUT_QUIET;
			++arguments;
			--count;
		} else if (strcmp(arguments[0], "--verbose") == 0) {
			g_output_mode = OUTPUT_VERBOSE;
			++arguments;
			--count;
		} else if (strcmp(arguments[0], "--debug") == 0) {
			g_output_mode = OUTPUT_DEBUG;
			++arguments;
			--count;
		} else if (strcmp(arguments[0], "--json") == 0) {
			g_output_mode = OUTPUT_JSON;
			g_color_enabled = 0;
			++arguments;
			--count;
		} else if (strcmp(arguments[0], "--no-color") == 0) {
			g_color_enabled = 0;
			++arguments;
			--count;
		} else {
			break;
		}
	}

	if (count == 1 && strcmp(arguments[0], "list") == 0)
		return list_packages() == 0 ? 0 : 1;
	if (count == 1 && strcmp(arguments[0], "env") == 0)
		return cmd_env();
	if (count == 1 && strcmp(arguments[0], "lint") == 0)
		return cmd_lint(0, NULL);
	if (count >= 2 && strcmp(arguments[0], "lint") == 0)
		return cmd_lint(count - 1, arguments + 1);
	if (count >= 1 && strcmp(arguments[0], "template") == 0)
		return cmd_template(count - 1, arguments + 1);
	if (count >= 1 && strcmp(arguments[0], "import") == 0) {
		arguments++; count--;
		return cmd_import(count, arguments);
	}
	if (count == 2 && strcmp(arguments[0], "show") == 0)
		return cmd_show(count - 1, arguments + 1);
	if (count >= 1 && strcmp(arguments[0], "init") == 0)
		return cmd_init(count - 1, arguments + 1);
	if (count >= 1 && strcmp(arguments[0], "pkg-config") == 0)
		return cmd_pkg_config(count - 1, arguments + 1);
	if (count == 1 && strcmp(arguments[0], "--bootstrap") == 0) {
		if (run("CC=\"${CC:-cc}\" make -C meow clean all") != 0)
			return 1;
		meow_msg(MSG_SUCCESS, "bootstrap build complete");
		return 0;
	}
	if (count == 1 && strcmp(arguments[0], "--help") == 0) {
		printf("usage: meow [-jN] [--arch <arch>] [--target <triple>] build <package> [target]\n");
		printf("       meow [-jN] [--arch <arch>] [--target <triple>] build        # auto 当前目录\n");
		printf("       meow [-jN] [--arch <arch>] [--target <triple>] build all\n");
		printf("       meow [-jN] [--arch <arch>] [--target <triple>] clean <package>\n");
		printf("       meow [-jN] [--arch <arch>] env        # 打印构建环境\n");
		printf("       meow list               # 列出可用包\n");
		printf("       meow init [<pkg>]       # 生成 .meow 配方\n");
		printf("       meow lint [<package>]   # 配方语法检查\n");
		printf("       meow show <package>     # 配方信息预览\n");
		printf("       meow pkg-config <lib> [--cflags|--libs]  # 已知库 flags 查询\n");
		printf("       meow --bootstrap\n");
		printf("       meow --help\n");
		printf("\n选项:\n");
		printf("  --arch=<arch>   目标架构（x86_64 / aarch64 等，默认 auto-detect）\n");
		printf("  --target=<triple>  完整 triple（如 x86_64-v3-meuos-linux）\n");
		printf("  -jN             并行任务数\n");
		printf("  --quiet         仅显示错误\n");
		printf("  --verbose       显示详细构建命令\n");
		printf("  --debug         显示所有调试信息\n");
		printf("  --json          以 JSON 格式输出\n");
		printf("  --no-color      禁用颜色（管道模式自动禁用）\n");
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
			meow_msg(MSG_INFO, "building %s", entry->d_name);
			char cmd[1024];
			snprintf(cmd, sizeof(cmd), "meow build %s", entry->d_name);
			if (run(cmd) != 0) {
				meow_msg(MSG_ERROR, "%s failed", entry->d_name);
				failures++;
			}
		}
		closedir(dir);
		meow_msg(failures ? MSG_ERROR : MSG_SUCCESS,
		         "built all packages (%d failures)", failures);
		return failures ? 1 : 0;
	}
	/* Zero-argument build: `meow build` with no package builds the current
	 * directory's recipe (auto-detect mode). */
	if (count == 1 && strcmp(arguments[0], "build") == 0) {
		data = malloc(RECIPE_MAX);
		if (!data) {
			meow_msg(MSG_ERROR, "out of memory");
			return 1;
		}
		if (load_recipe(".", path, sizeof(path), data) < 0) {
			meow_msg(MSG_ERROR, "no build recipe in current directory; specify a package");
			free(data);
			return 1;
		}
		/* Dispatch parser by extension */
		int rc;
		size_t plen = strlen(path);
		if (plen >= 5 && strcmp(path + plen - 5, ".meow") == 0)
			rc = parse_meow(data);
		else
			rc = parse_recipe(data);
		if (rc != 0) {
			meow_msg(MSG_ERROR, "invalid recipe in current directory");
			free(data);
			return 1;
		}
		set_arch_env();
		{	char *build_dir = getenv("BLD_DIR");
			if (probe_run(build_dir ? build_dir : ".") != 0) {
				meow_msg(MSG_ERROR, "probe failed for current directory");
				free(data);
				return 1;
			}
		}
		requested = default_target;
		if (!requested)
			requested = find_target("all") ? "all" : "build";
		expand_uses();
		if (run_target(find_target(requested)) != 0) {
			meow_msg(MSG_ERROR, "target failed: %s (%s)", requested, path);
			free(data);
			return 1;
		}
		meow_msg(MSG_SUCCESS, "built %s", path);
		free(data);
		return 0;
	}
	if ((count != 2 && count != 3) ||
	    (strcmp(arguments[0], "build") != 0 && strcmp(arguments[0], "clean") != 0)) {
		printf("usage: meow [-jN] [--arch <arch>] [--target <triple>] build <package> [target] | meow [-jN] [--arch <arch>] build | meow [-jN] [--arch <arch>] clean <package> | meow list | meow init [<pkg>] | meow lint [<package>] | meow show <package> | meow --bootstrap\n");
		return 2;
	}
	data = malloc(RECIPE_MAX);
	if (!data) {
		printf("meow: out of memory\n");
		return 1;
	}
	if (load_recipe(arguments[1], path, sizeof(path), data) < 0 && strcmp(arguments[1], ".") == 0 && strcmp(arguments[0], "build") == 0) {
		if (run("CC=mcc DESTDIR=\"${MEUOS_SYSROOT:-./sysroot}\" PREFIX=/usr make") != 0) {
			meow_msg(MSG_ERROR, "Makefile compatibility build failed");
			return 1;
		}
		meow_msg(MSG_SUCCESS, "built current Makefile (compatibility mode)");
		return 0;
	}
	if (load_recipe(arguments[1], path, sizeof(path), data) < 0) {
		meow_msg(MSG_ERROR, "invalid or unreadable build file for %s", arguments[1]);
		return 1;
	}
	/* Dispatch to the right parser based on file extension */
	size_t plen = strlen(path);
	int is_meow = (plen >= 5 && strcmp(path + plen - 5, ".meow") == 0);
	if (is_meow) {
		if (parse_meow(data) != 0) {
			meow_msg(MSG_ERROR, "invalid .meow recipe: %s", path);
			return 1;
		}
	} else {
		if (parse_recipe(data) != 0) {
			meow_msg(MSG_ERROR, "invalid recipe: %s", path);
			return 1;
		}
	}
	/* Inject ARCH into recipe environment after parse. */
	set_arch_env();

	/* Inject path variables (%PKGDIR%, %BUILDDIR%, %WORKDIR%) */
	{
		char pwd[1024];
		if (getcwd(pwd, sizeof(pwd))) {
			char envbuf[2048];
			size_t len = strlen(recipe_environment);
			const char *bld = getenv("BLD_DIR");
			const char *wrk = getenv("WORK_DIR");
			snprintf(envbuf, sizeof(envbuf),
			         "export PKGDIR='%s/pkgs/%s'; "
			         "export BUILDDIR='%s'; "
			         "export WORKDIR='%s'; "
			         "export SRCDIR='%s/pkgs/%s'; ",
			         pwd, arguments[1],
			         bld ? bld : pwd,
			         wrk ? wrk : "/tmp/meow-build",
			         pwd, arguments[1]);
			if (len + strlen(envbuf) < sizeof(recipe_environment))
				memcpy(recipe_environment + len, envbuf,
				       strlen(envbuf) + 1);
		}
	}

	/* Run inline feature detection (probe section) if any probes are
	 * registered.  The generated config.h is placed in the current
	 * directory; recipes reference it via -I. or $PWD. */
	{	char *build_dir = getenv("BLD_DIR");
		if (probe_run(build_dir ? build_dir : ".") != 0) {
			meow_msg(MSG_ERROR, "probe failed for %s", arguments[1]);
			return 1;
		}
	}
	/* Resolve cross-package dependencies (meow-subdirs): for each package
	 * listed in recipe's `depends:` section, recursively build it first. */
	for (size_t di = 0; di < nrecipe_deps; ++di) {
		char dep_cmd[1024];
		snprintf(dep_cmd, sizeof dep_cmd, "%s build %s",
		         meow_self ? meow_self : "meow", recipe_deps[di]);
		if (run(dep_cmd) != 0) {
			meow_msg(MSG_ERROR, "dependency failed: %s", recipe_deps[di]);
			return 1;
		}
	}

	requested = strcmp(arguments[0], "clean") == 0 ? "clean" : count == 3 ? arguments[2] : default_target;
	if (!requested)
		requested = find_target("all") ? "all" : "build";
	expand_uses();
	if (run_target(find_target(requested)) != 0) {
		meow_msg(MSG_ERROR, "target failed: %s (%s)", requested, path);
		return 1;
	}
	meow_msg(MSG_SUCCESS, "%s %s%s%s",
	         strcmp(arguments[0], "clean") == 0 ? "cleaned" : "built",
	         arguments[1], count == 3 ? " target " : "", count == 3 ? arguments[2] : "");
	return 0;
}
