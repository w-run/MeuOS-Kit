/* meow - dependency-graph execution.
 *
 * run_target() is the public entry; target_out_of_date(), file_mtime(),
 * newer(), expand_command() and append_text() are file-local helpers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

#include "meow.h"

/* Forward declaration of expand_env_vars from parse.c */

/* Progress counters (declared extern in meow.h; defined here). */
int total_commands = 0;
int completed_commands = 0;

/* Log file handle, kept open across run_target() for real-time logging. */
static FILE *g_log_fp = NULL;

/* Expand ${VAR} in path, using a thread-local buffer for the result.
 * Returns the original path if no expansion is needed, or a pointer
 * to a TLS buffer with the expanded result. */
static const char *
expand_path(const char *path)
{
	static _Thread_local char buffer[RECIPE_MAX];
	if (strchr(path, '$') && strchr(path, '{')) {
		if (expand_env_vars(path, buffer, sizeof(buffer)) == 0)
			return buffer;
	}
	return path;
}

/* Wrap s in single quotes, escaping any embedded single quote as '\''
 * (close-quote + escaped quote + reopen-quote) so the result is a single
 * shell word even when s contains quotes.  Returns 0 on success, -1 if
 * the escaped form would not fit in the buffer. */
static int
shell_quote(char *out, size_t size, const char *s)
{
	size_t pos = 0;
	if (size < 3) return -1;
	out[pos++] = '\'';
	for (; *s; s++) {
		if (*s == '\'') {
			if (pos + 4 >= size) return -1;
			out[pos++] = '\'';
			out[pos++] = '\\';
			out[pos++] = '\'';
			out[pos++] = '\'';
		} else {
			if (pos + 1 >= size) return -1;
			out[pos++] = *s;
		}
	}
	if (pos + 1 >= size) return -1;
	out[pos++] = '\'';
	out[pos] = '\0';
	return 0;
}

/* Cross-process target lock.  run_target() recurses via fork(): each child
 * inherits a private copy of the target table, so the target->done flags
 * are not shared and two sibling processes can build the same transitive
 * dependency twice.  A per-target flock() in /tmp serializes the build
 * decision across processes; callers re-check out-of-date after acquiring
 * the lock so a target already produced by another process is skipped.
 * Lock files are intentionally left in place (unlink would race). */
static int
target_lock(const char *name)
{
	unsigned long h = 5381;
	for (const char *p = name; *p; p++)
		h = ((h << 5) + h) + (unsigned char)*p;
	char path[128];
	snprintf(path, sizeof(path), "/tmp/meow-lock-%08lx", h);
	int fd = open(path, O_CREAT | O_RDWR, 0600);
	if (fd < 0)
		return -1;
	flock(fd, LOCK_EX);
	return fd;
}

static void
target_unlock(int fd)
{
	if (fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
}

/* Evaluate a "when:" condition expression.
 * Returns 1 if condition is met (or no condition), 0 if skipped. */
static int
eval_condition(const char *when_expr)
{
	if (!when_expr || !*when_expr)
		return 1;

	/* Parse: EXPR OP "VALUE" */
	char expr[64], op[8], value[128];
	if (sscanf(when_expr, "%63[^ ] %7[^ ] \"%127[^\"]\"", expr, op, value) < 3)
		return 1;  /* unparseable = true (graceful fallback) */

	const char *actual = NULL;
	if (strcmp(expr, "ARCH") == 0)
		actual = build_arch;
	else if (strcmp(expr, "TARGET") == 0)
		actual = build_target;

	if (!actual)
		return 1;  /* unknown variable = true (graceful fallback) */

	if (strcmp(op, "==") == 0)
		return strcmp(actual, value) == 0;
	if (strcmp(op, "!=") == 0)
		return strcmp(actual, value) != 0;
	return 1;  /* unknown op = true */
}

/* File-local helpers (defined below; run_target() calls them). */
static int target_out_of_date(struct target *);
static int expand_command(struct target *, const char *, char *, size_t);
void expand_uses(void);  /* called from main.c */

/* Draw a progress bar to stdout. Called after each command completes
 * during parallel builds. */
static void
show_progress(const char *current)
{
	if (g_output_mode == OUTPUT_QUIET || g_output_mode == OUTPUT_JSON)
		return;
	int pct = total_commands ? (completed_commands * 100 / total_commands) : 0;
	int width = 12;
	int filled = width * pct / 100;
	/* Clear the current line, then draw progress. */
	fprintf(stdout, "\r\033[K  [");
	for (int i = 0; i < width; i++)
		fprintf(stdout, "%s", i < filled ? "█" : "░");
	if (current)
		fprintf(stdout, "] %d%% (%d/%d) · %s", pct, completed_commands, total_commands, current);
	else
		fprintf(stdout, "] %d%% (%d/%d)", pct, completed_commands, total_commands);
	fflush(stdout);
	if (completed_commands >= total_commands)
		fprintf(stdout, "\n");
}

/* Count total commands across all targets once. */
static void
count_total_commands(void)
{
	if (total_commands > 0)
		return;
	for (size_t i = 0; i < ntargets; i++)
		total_commands += targets[i].ncommands;
}

/* Expand uses: entries from recipe into environment variables.
 * Called once per build, before any target executes. */
void
expand_uses(void)
{
	if (nuses == 0)
		return;

	char libs_buf[4096] = {0};
	char cflags_buf[4096] = {0};
	size_t libs_pos = 0, cflags_pos = 0;

	for (size_t i = 0; i < nuses; i++) {
		char pc_cflags[4096], pc_libs[4096];
		const char *lib_cflags = NULL;
		const char *lib_libs = NULL;

		/* 优先尝试 .pc 文件 */
		if (pkg_config_lookup(uses[i], pc_cflags, sizeof(pc_cflags),
		                      pc_libs, sizeof(pc_libs)) == 0) {
			lib_cflags = pc_cflags;
			lib_libs = pc_libs;
		} else {
			/* 回退到 known_libs 表 */
			const struct pkg_lib *lib = find_lib(uses[i]);
			if (!lib) {
				meow_msg(MSG_WARN, "uses: unknown library '%s', skipping", uses[i]);
				continue;
			}
			lib_cflags = lib->cflags;
			lib_libs = lib->libs;
		}

		/* Append to aggregate LIBS / CFLAGS */
		if (lib_libs && lib_libs[0]) {
			if (libs_pos > 0 && libs_pos < sizeof(libs_buf) - 1)
				libs_buf[libs_pos++] = ' ';
			size_t len = strlen(lib_libs);
			if (libs_pos + len < sizeof(libs_buf)) {
				memcpy(libs_buf + libs_pos, lib_libs, len);
				libs_pos += len;
			}
		}
		if (lib_cflags && lib_cflags[0]) {
			if (cflags_pos > 0 && cflags_pos < sizeof(cflags_buf) - 1)
				cflags_buf[cflags_pos++] = ' ';
			size_t len = strlen(lib_cflags);
			if (cflags_pos + len < sizeof(cflags_buf)) {
				memcpy(cflags_buf + cflags_pos, lib_cflags, len);
				cflags_pos += len;
			}
		}

		/* Export individual PKG_<NAME>_LIBS and PKG_<NAME>_CFLAGS */
		char varname[128], envbuf[512];
		snprintf(varname, sizeof(varname), "PKG_%s_LIBS", uses[i]);
		for (char *p = varname + 4; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p &= ~0x20;
		snprintf(envbuf, sizeof(envbuf), "export %s='%s'; ", varname, lib_libs ? lib_libs : "");
		if (strlen(recipe_environment) + strlen(envbuf) < RECIPE_ENV_MAX)
			strcat(recipe_environment, envbuf);
		setenv(varname, lib_libs ? lib_libs : "", 1);

		snprintf(varname, sizeof(varname), "PKG_%s_CFLAGS", uses[i]);
		for (char *p = varname + 4; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p &= ~0x20;
		snprintf(envbuf, sizeof(envbuf), "export %s='%s'; ", varname, lib_cflags ? lib_cflags : "");
		if (strlen(recipe_environment) + strlen(envbuf) < RECIPE_ENV_MAX)
			strcat(recipe_environment, envbuf);
		setenv(varname, lib_cflags ? lib_cflags : "", 1);
	}

	/* Aggregate LIBS and CFLAGS for %LIBS% / %CFLAGS% interpolation */
	libs_buf[libs_pos] = '\0';
	cflags_buf[cflags_pos] = '\0';
	char agg[2048];
	snprintf(agg, sizeof(agg), "export LIBS='%s'; export CFLAGS='%s'; ",
	         libs_buf, cflags_buf);
	if (strlen(recipe_environment) + strlen(agg) < RECIPE_ENV_MAX)
		strcat(recipe_environment, agg);
	setenv("LIBS", libs_buf, 1);
	setenv("CFLAGS", cflags_buf, 1);
}

int
run_target(struct target *target)
{
	if (!target || target->visiting)
		return -1;
	if (target->done)
		return 0;
	/* Skip if condition not met */
	if (target->when && !eval_condition(target->when)) {
		target->done = 1;
		return 0;
	}
	target->visiting = 1;
	if (parallel_jobs > 1) {
		pid_t children[TARGET_DEPS_MAX];
		struct target *child_targets[TARGET_DEPS_MAX];
		size_t active = 0;
		int failed = 0;
		count_total_commands();

		/* Collect unique direct dependencies — skip already-done and
		 * duplicate entries so the same target is never forked twice. */
		struct target *uniques[TARGET_DEPS_MAX];
		size_t nuniques = 0;
		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (!dependency || dependency->done)
				continue;
			int skip = 0;
			for (size_t j = 0; j < nuniques; j++)
				if (uniques[j] == dependency) { skip = 1; break; }
			if (!skip)
				uniques[nuniques++] = dependency;
		}

		for (size_t i = 0; i < nuniques; ++i) {
			struct target *dependency = uniques[i];
			while (active == (size_t)parallel_jobs) {
				int status;
				pid_t done = waitpid(-1, &status, 0);
				for (size_t j = 0; j < active; ++j)
					if (children[j] == done) {
						child_targets[j]->done = WIFEXITED(status) && WEXITSTATUS(status) == 0;
						if (!child_targets[j]->done) failed = 1;
						children[j] = children[--active];
						child_targets[j] = child_targets[active];
						break;
					}
				if (done < 0) return -1;
			}
			children[active] = fork();
			if (children[active] < 0)
				return -1;
			if (!children[active])
				_exit(run_target(dependency) == 0 ? 0 : 1);
			child_targets[active++] = dependency;
		}
		while (active) {
			int status;
			pid_t done = waitpid(-1, &status, 0);
			for (size_t j = 0; j < active; ++j)
				if (children[j] == done) {
					child_targets[j]->done = WIFEXITED(status) && WEXITSTATUS(status) == 0;
					if (!child_targets[j]->done) failed = 1;
					children[j] = children[--active];
					child_targets[j] = child_targets[active];
					break;
				}
			if (done < 0) return -1;
		}
		if (failed)
			return -1;
	} else {
		/* Serial execution: dedup and skip already-done deps */
		struct target *uniques[TARGET_DEPS_MAX];
		size_t nuniques = 0;
		for (size_t i = 0; i < target->ndeps; ++i) {
			struct target *dependency = find_target(target->deps[i]);
			if (!dependency || dependency->done)
				continue;
			int skip = 0;
			for (size_t j = 0; j < nuniques; j++)
				if (uniques[j] == dependency) { skip = 1; break; }
			if (!skip)
				uniques[nuniques++] = dependency;
		}
		for (size_t i = 0; i < nuniques; ++i) {
			if (run_target(uniques[i]) != 0)
				return -1;
		}
	}
	/* ———— 架构过滤 ———— */
	/* only: x86_64, aarch64 — 架构白名单 */
	if (target->only_arch) {
		char buf[256];
		strncpy(buf, target->only_arch, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		int matched = 0;
		char *p = buf;
		while (*p) {
			while (*p == ' ' || *p == ',') { *p = '\0'; p++; }
			if (!*p) break;
			char *start = p;
			while (*p && *p != ',') p++;
			if (*p == ',') { *p = '\0'; p++; }
			if (build_arch && strcmp(start, build_arch) == 0) { matched = 1; break; }
		}
		if (!matched) {
			meow_msg(MSG_DEBUG, "target '%s' skipped (only: %s)", target->name, target->only_arch);
			target->visiting = 0; target->done = 1; return 0;
		}
	}
	/* except: arm — 架构黑名单 */
	if (target->except_arch) {
		char buf[256];
		strncpy(buf, target->except_arch, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		int matched = 0;
		char *p = buf;
		while (*p) {
			while (*p == ' ' || *p == ',') { *p = '\0'; p++; }
			if (!*p) break;
			char *start = p;
			while (*p && *p != ',') p++;
			if (*p == ',') { *p = '\0'; p++; }
			if (build_arch && strcmp(start, build_arch) == 0) { matched = 1; break; }
		}
		if (matched) {
			meow_msg(MSG_DEBUG, "target '%s' skipped (except: %s)", target->name, target->except_arch);
			target->visiting = 0; target->done = 1; return 0;
		}
	}
	/* ———— 语义节执行 ———— */
	/* has: tool1, tool2 — 检测构建工具 */
	if (nhas_tools_stack > 0) {
		for (size_t hi = 0; hi < nhas_tools_stack; hi++) {
			char cmd[256];
			snprintf(cmd, sizeof(cmd), "command -v '%s' >/dev/null 2>&1", has_tools_stack[hi]);
			if (run(cmd) != 0) {
				meow_msg(MSG_ERROR, "required tool not found: %s", has_tools_stack[hi]);
				return -1;
			}
			meow_msg(MSG_DEBUG, "tool found: %s", has_tools_stack[hi]);
		}
	}
	/* download: URL — 下载源文件（写入 /tmp，与 sha256/unpack 路径一致） */
	if (target->download_url) {
		const char *fname = strrchr(target->download_url, '/');
		fname = fname ? fname + 1 : target->download_url;
		char dlpath[512];
		snprintf(dlpath, sizeof(dlpath), "/tmp/%s", fname);
		struct stat st;
		if (stat(dlpath, &st) != 0) {
			/* URL/路径经单引号转义，防止注入 */
			char q_url[2048], q_path[2048];
			if (shell_quote(q_url, sizeof(q_url), target->download_url) != 0 ||
			    shell_quote(q_path, sizeof(q_path), dlpath) != 0)
				return -1;
			char cmd[4096];
			snprintf(cmd, sizeof(cmd), "curl -sSL -o %s %s 2>/dev/null || wget -q %s -O %s",
			         q_path, q_url, q_url, q_path);
			if (run(cmd) != 0) {
				meow_msg(MSG_ERROR, "download failed: %s", target->download_url);
				return -1;
			}
		}
	}
	/* ———— 宏执行（构建前） ———— */
	/* sha256: 下载文件校验 */
	if (target->download_sha256 && target->download_url) {
		const char *fname = strrchr(target->download_url, '/');
		fname = fname ? fname + 1 : target->download_url;
		char dlpath[512];
		snprintf(dlpath, sizeof(dlpath), "/tmp/%s", fname);
		char check[640];
		snprintf(check, sizeof(check), "%s  %s", target->download_sha256, dlpath);
		char q_check[768];
		if (shell_quote(q_check, sizeof(q_check), check) != 0)
			return -1;
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "echo %s | sha256sum -c - >/dev/null 2>&1", q_check);
		if (run(cmd) != 0) {
			meow_msg(MSG_ERROR, "SHA-256 mismatch for %s", fname);
			return -1;
		}
	}
	/* unpack: 自动解压 */
	if (target->unpack && target->download_url) {
		const char *fname = strrchr(target->download_url, '/');
		fname = fname ? fname + 1 : target->download_url;
		char tar_path[512];
		snprintf(tar_path, sizeof(tar_path), "/tmp/%s", fname);
		char q_tar[768];
		if (shell_quote(q_tar, sizeof(q_tar), tar_path) != 0)
			return -1;
		size_t tlen = strlen(tar_path);
		char cmd[1024];
		if (tlen > 7 && strcmp(tar_path + tlen - 7, ".tar.gz") == 0)
			snprintf(cmd, sizeof(cmd), "mkdir -p /tmp/meow-build && tar xzf %s -C /tmp/meow-build", q_tar);
		else if (tlen > 8 && strcmp(tar_path + tlen - 8, ".tar.xz") == 0)
			snprintf(cmd, sizeof(cmd), "mkdir -p /tmp/meow-build && tar xJf %s -C /tmp/meow-build", q_tar);
		else if (tlen > 7 && strcmp(tar_path + tlen - 7, ".tar.bz2") == 0)
			snprintf(cmd, sizeof(cmd), "mkdir -p /tmp/meow-build && tar xjf %s -C /tmp/meow-build", q_tar);
		else if (tlen > 4 && strcmp(tar_path + tlen - 4, ".zip") == 0)
			snprintf(cmd, sizeof(cmd), "mkdir -p /tmp/meow-build && unzip -o %s -d /tmp/meow-build", q_tar);
		else
			snprintf(cmd, sizeof(cmd), "echo 'unpack: unknown format: %s'", tar_path);
		if (run(cmd) != 0) return -1;
	}
	/* patch: 应用补丁 */
	for (size_t pi = 0; pi < target->npatch; pi++) {
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "patch -p1 -i '%s' 2>/dev/null || true", target->patches[pi]);
		run(cmd);
	}
	/* srcdir / builddir: 确保目录存在 */
	if (target->src_dir) {
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", target->src_dir);
		run(cmd);
	}
	if (target->build_dir) {
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", target->build_dir);
		run(cmd);
	}
	/* parallel: 覆盖并行度 */
	int saved_parallel = parallel_jobs;
	if (target->parallel_jobs > 0)
		parallel_jobs = target->parallel_jobs;
	if (target->log_file) {
		/* Open log file for writing and keep it open for the duration
		 * of the build so setvbuf takes effect. */
		g_log_fp = fopen(target->log_file, "w");
		if (g_log_fp)
			setvbuf(g_log_fp, NULL, _IONBF, 0);
	}
	/* 并行防重复构建：run_target() 递归经 fork 复制内存，target->done 标志
	 * 无法跨进程共享，兄弟子进程可能重复构建同一个共享传递依赖（竞态）。
	 * 轻量防护：执行目标命令前加 per-target flock，获得锁后重新检查
	 * out-of-date —— 若另一进程已完成（产出文件已生成）则跳过构建。 */
	int lockfd = -1;
	if (parallel_jobs > 1)
		lockfd = target_lock(target->name);
	int do_build = target_out_of_date(target);
	if (do_build && lockfd >= 0)
		do_build = target_out_of_date(target);  /* 锁内重新检查 */
	if (do_build) {
		/* pre: 前置钩子 */
		if (target->pre_cmd) {
			char cmd[RECIPE_MAX];
			expand_command(target, target->pre_cmd, cmd, sizeof(cmd));
			if (run(cmd) != 0) return -1;
		}
		for (size_t i = 0; i < target->ncommands; ++i)
			{
				/* 该命令自己的 run(X) 修饰符（run 级），未设置时回退到
				 * target 级默认。 */
				unsigned char cflags = (i < TARGET_COMMANDS_MAX) ?
					target->cmd_flags[i] : 0;
				int cmd_quiet = target->run_quiet;
				int cmd_abort = target->run_abort_on_fail;
				if (cflags & CMD_F_QUIET) cmd_quiet = 1;
				if (cflags & CMD_F_ABORT) cmd_abort = 1;
				else if (cflags & CMD_F_NOABORT) cmd_abort = 0;

				char command[RECIPE_MAX];
				if (expand_command(target, target->commands[i], command,
					sizeof(command)) != 0)
					return -1;
				/* workdir: 前置 cd 命令 */
				char full_cmd[RECIPE_MAX + 128];
				const char *exec_cmd;
				if (target->work_dir) {
					snprintf(full_cmd, sizeof(full_cmd), "mkdir -p '%s' && cd '%s' && %s",
					         target->work_dir, target->work_dir, command);
					exec_cmd = full_cmd;
				} else {
					exec_cmd = command;
				}
				if (cmd_quiet) {
					char *buf = malloc(strlen(exec_cmd) + 32);
					sprintf(buf, "%s >/dev/null 2>&1", exec_cmd);
					int rc = run(buf);
					free(buf);
					if (rc != 0 && cmd_abort) {
						if (target->error_cmd) {
							char ec[RECIPE_MAX];
							setenv("CMD", exec_cmd, 1);
							setenv("EXITCODE", "1", 1);
							expand_command(target, target->error_cmd, ec, sizeof(ec));
							run(ec);
						}
						return -1;
					}
				} else {
					int rc = run(exec_cmd);
					if (rc != 0 && cmd_abort) {
						if (target->error_cmd) {
							char ec[RECIPE_MAX];
							setenv("CMD", exec_cmd, 1);
							setenv("EXITCODE", "1", 1);
							expand_command(target, target->error_cmd, ec, sizeof(ec));
							run(ec);
						}
						return -1;
					}
				}
				completed_commands++;
				if (parallel_jobs > 1)
					show_progress(target->name);
			}
	}
	else {
		/* Target is up-to-date, still count for progress. */
		completed_commands += target->ncommands;
	}
	if (lockfd >= 0)
		target_unlock(lockfd);
	/* ———— 宏执行（构建后） ———— */
	/* post: 后置钩子 */
	if (target->post_cmd) {
		char cmd[RECIPE_MAX];
		expand_command(target, target->post_cmd, cmd, sizeof(cmd));
		if (run(cmd) != 0)
			meow_msg(MSG_WARN, "post-hook failed: %s", target->post_cmd);
	}
	/* test: 运行测试（遇错继续） */
	if (target->test_cmd) {
		char cmd[RECIPE_MAX];
		expand_command(target, target->test_cmd, cmd, sizeof(cmd));
		if (run(cmd) != 0)
			meow_msg(MSG_WARN, "test failed for target '%s'", target->name);
	}
	/* install: 声明式安装 */
	if (target->install_src && target->install_dest) {
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "mkdir -p $(dirname '%s') && cp '%s' '%s'",
		         target->install_dest, target->install_src, target->install_dest);
		if (run(cmd) != 0) return -1;
		if (target->install_mode) {
			snprintf(cmd, sizeof(cmd), "chmod %o '%s'", target->install_mode, target->install_dest);
			run(cmd);
		}
	}
	/* strip: 去除调试符号（独立于 copy:，或自动使用 copy: 目标路径） */
	const char *strip_target = target->install_dest;
	if (target->strip && !strip_target)
		strip_target = target->install_src;  /* fallback to src */
	if (target->strip && strip_target) {
		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "strip '%s' 2>/dev/null || true", strip_target);
		run(cmd);
	}
	/* 恢复全局并行度 */
	if (target->parallel_jobs > 0)
		parallel_jobs = saved_parallel;
	if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
	target->visiting = 0;
	target->done = 1;
	return 0;
}

static int
file_mtime(const char *path, time_t *seconds, long *nanoseconds)
{
	struct stat status;

	if (stat(path, &status) < 0)
		return -1;
	*seconds = status.st_mtim.tv_sec;
	*nanoseconds = status.st_mtim.tv_nsec;
	return 0;
}

static void
newer(time_t seconds, long nanoseconds, time_t *latest_seconds, long *latest_nanoseconds)
{
	if (seconds > *latest_seconds ||
	 (seconds == *latest_seconds && nanoseconds > *latest_nanoseconds)) {
		*latest_seconds = seconds;
		*latest_nanoseconds = nanoseconds;
	}
}

static int
target_out_of_date(struct target *target)
{
	time_t newest_seconds = 0;
	long newest_nanoseconds = 0;
	time_t oldest_seconds = 0;
	long oldest_nanoseconds = 0;
	size_t i;

	if (target->phony || !target->noutputs)
		return 1;
	for (i = 0; i < target->ninputs; ++i) {
		time_t seconds;
		long nanoseconds;
		if (file_mtime(expand_path(target->inputs[i]), &seconds, &nanoseconds) < 0)
			return 1;
		newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
	}
	for (i = 0; i < target->ndeps; ++i) {
		struct target *dependency = find_target(target->deps[i]);
		if (dependency) {
			for (size_t j = 0; j < dependency->noutputs; ++j) {
				long seconds, nanoseconds;
				if (file_mtime(expand_path(dependency->outputs[j]), &seconds, &nanoseconds) < 0)
					return 1;
				newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
			}
		} else {
			time_t seconds;
			long nanoseconds;
			if (file_mtime(expand_path(target->deps[i]), &seconds, &nanoseconds) < 0)
				return 1;
			newer(seconds, nanoseconds, &newest_seconds, &newest_nanoseconds);
		}
	}
	for (i = 0; i < target->noutputs; ++i) {
		time_t seconds;
		long nanoseconds;
		if (file_mtime(expand_path(target->outputs[i]), &seconds, &nanoseconds) < 0)
			return 1;
		if (!i || seconds < oldest_seconds ||
		 (seconds == oldest_seconds && nanoseconds < oldest_nanoseconds)) {
			oldest_seconds = seconds;
			oldest_nanoseconds = nanoseconds;
		}
	}
	return newest_seconds > oldest_seconds ||
	 (newest_seconds == oldest_seconds && newest_nanoseconds > oldest_nanoseconds);
}

static int
append_text(char *buffer, size_t size, size_t *length, const char *text)
{
	size_t text_length = strlen(text);

	if (*length + text_length + 1 > size)
		return -1;
	memcpy(buffer + *length, text, text_length);
	*length += text_length;
	buffer[*length] = 0;
	return 0;
}

/* Expand the three Make-compatible automatic variables in a command.  Shell
 * variables remain untouched; `$$` escapes a literal dollar for the shell. */
static int
expand_command(struct target *target, const char *command, char *buffer, size_t size)
{
	size_t length = 0;
	const char *first = target->ndeps ? target->deps[0] :
		target->ninputs ? target->inputs[0] : "";

	buffer[0] = 0;
	while (*command) {
		if (*command != '$' || !command[1]) {
			char character[2] = { *command++, 0 };
			if (append_text(buffer, size, &length, character) < 0)
				return -1;
			continue;
		}
		++command;
		if (*command == '@') {
			if (append_text(buffer, size, &length, target->name) < 0)
				return -1;
		} else if (*command == '<') {
			if (append_text(buffer, size, &length, first) < 0)
				return -1;
		} else if (*command == '^') {
			for (size_t i = 0; i < target->ndeps + target->ninputs; ++i) {
				const char *item = i < target->ndeps ? target->deps[i] :
					target->inputs[i - target->ndeps];
				if (i && append_text(buffer, size, &length, " ") < 0)
					return -1;
				if (append_text(buffer, size, &length, item) < 0)
					return -1;
			}
		} else if (*command == '*') {
			if (append_text(buffer, size, &length, target->stem ? target->stem : "") < 0)
				return -1;
		} else if (*command == '$') {
			if (append_text(buffer, size, &length, "$") < 0)
				return -1;
		} else {
			char characters[3] = { '$', *command, 0 };
			if (append_text(buffer, size, &length, characters) < 0)
				return -1;
		}
		++command;
	}
	return 0;
}

