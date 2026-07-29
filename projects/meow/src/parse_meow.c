/* parse_meow.c — .meow 格式解析器
 *
 * 格式设计：
 *   # 注释
 *   key: value             键值（全局或节区内）
 *   [section]              节区标题
 *   key:                   多行块（如 run: 后的 shell 脚本）
 *     line 1
 *     line 2
 *   deps: a, b, c          逗号分隔列表
 *   %VAR%                 变量插值
 *
 * 解析结果存入 meow 的 target[] / recipe_environment / recipe_deps 全局。
 */
#include "meow.h"
#include "triple.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* 当前解析状态 */

enum meow_section { SEC_NONE, SEC_PROBE, SEC_VARIABLES, SEC_DEPENDS };

static struct target *current_target;
static enum meow_section current_section;
static int in_runblock;    /* 收集 run: 块的行 */
static char runblock_buf[32768];
static size_t runblock_len;

/* 探测子节区（[probe] 内部） */
static int in_probe_list;
static char probe_list_type[32];

/* Forward declarations */
static void interpolate(const char *in, char *out, size_t outsz);

static void
flush_runblock(void)
{
	if (!current_target || runblock_len == 0)
		return;
	if (current_target->ncommands >= TARGET_COMMANDS_MAX)
		return;
	/* Interpolate %VAR% in run block before storing */
	char interpolated[sizeof(runblock_buf)];
	interpolate(runblock_buf, interpolated, sizeof(interpolated));
	/* 整个 run 块作为一条命令存入（执行时写入 temp sh 脚本） */
	current_target->commands[current_target->ncommands] =
		strdup(interpolated);
	if (current_target->commands[current_target->ncommands])
		current_target->ncommands++;
	runblock_len = 0;
	runblock_buf[0] = '\0';
}

static void
add_to_runblock(const char *line)
{
	size_t len = strlen(line);
	if (runblock_len + len + 2 >= sizeof(runblock_buf))
		return;
	memcpy(runblock_buf + runblock_len, line, len);
	runblock_len += len;
	runblock_buf[runblock_len++] = '\n';
	runblock_buf[runblock_len] = '\0';
}

/* 替换字符串中的 %VAR% 为环境变量 */
static void
interpolate(const char *in, char *out, size_t outsz)
{
	const char *p = in;
	size_t o = 0;
	while (*p && o < outsz - 1) {
		if (*p == '%') {
			const char *end = strchr(p + 1, '%');
			if (end && end > p + 1) {
				size_t varlen = (size_t)(end - p - 1);
				char varname[64];
				snprintf(varname, sizeof(varname), "%.*s", (int)varlen, p + 1);
				const char *val = getenv(varname);
				if (val) {
					size_t vallen = strlen(val);
					size_t space = outsz - o - 1;
					if (vallen > space) vallen = space;
					memcpy(out + o, val, vallen);
					o += vallen;
				}
				p = end + 1;
				continue;
			}
		}
		out[o++] = *p++;
	}
	out[o] = '\0';
}

/* 将 key:value 同时存入 recipe_environment（shell exec 用）和 setenv（%VAR% 用） */
static void
set_env(const char *key, const char *value)
{
	char envbuf[512];
	size_t len = strlen(recipe_environment);
	snprintf(envbuf, sizeof(envbuf), "export %s='%s'; ", key, value);
	if (len + strlen(envbuf) < sizeof(recipe_environment))
		memcpy(recipe_environment + len, envbuf, strlen(envbuf) + 1);
	setenv(key, value, 1);
}

/* 去除首尾空白 */
static char *
trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
	*e = '\0';
	return s;
}

/* 计算缩进（空格数） */
static int
indent_of(const char *s)
{
	int n = 0;
	while (*s == ' ') { n++; s++; }
	return n;
}

/* 添加一个 target */
static struct target *
add_target(const char *name)
{
	if (ntargets >= TARGET_MAX) return NULL;
	struct target *t = &targets[ntargets++];
	memset(t, 0, sizeof(*t));
	t->name = strdup(name);
	return t;
}

int
parse_meow(char *data)
{
	char *line = data;
	int state = 0;
	in_runblock = 0;
	runblock_len = 0;
	runblock_buf[0] = '\0';
	current_target = NULL;
	current_section = SEC_NONE;
	in_probe_list = 0;
	probe_list_type[0] = '\0';
	ntargets = 0;
	nuses = 0;
	nhas_tools_stack = 0;
	nlib_deps_stack = 0;
	default_target = NULL;

	while (*line) {
		char *end = line;
		while (*end && *end != '\n') end++;
		if (*end) *end++ = '\0';

		char *text = trim(line);
		int indent = indent_of(line);

		/* 跳过空行和注释 */
		if (!*text || *text == '#') {
			line = end;
			continue;
		}

		if (in_runblock) {
			/* 收集 run: 块的后续缩进行 — 空行也保持不中断块 */
			if (indent >= 2) {
				add_to_runblock(text);
				line = end;
				continue;
			} else if (!*text) {
				/* 空行：加入 runblock 保留换行，不中断 */
				add_to_runblock("");
				line = end;
				continue;
			} else {
				flush_runblock();
				in_runblock = 0;
			}
		}

		/* [section] — 构建目标或特殊节区 */
		if (text[0] == '[') {
			flush_runblock();
			char *closing = strchr(text, ']');
			if (!closing) return -1;
			*closing = '\0';
			const char *tname = trim(text + 1);

			/* 检查特殊节区 */
			if (strcmp(tname, "probe") == 0) {
				current_section = SEC_PROBE;
				current_target = NULL;
				probe_reset();
				in_probe_list = 0;
				probe_list_type[0] = '\0';
				line = end;
				continue;
			}
			if (strcmp(tname, "variables") == 0 || strcmp(tname, "env") == 0) {
				current_section = SEC_VARIABLES;
				current_target = NULL;
				line = end;
				continue;
			}
			if (strcmp(tname, "depends") == 0) {
				current_section = SEC_DEPENDS;
				current_target = NULL;
				line = end;
				continue;
			}

			/* 普通构建目标 */
			current_section = SEC_NONE;
			in_probe_list = 0;
			current_target = add_target(tname);
			if (!current_target) return -1;
			state = 1;
			line = end;
			continue;
		}

		/* key: value 或 key: block */
		char *colon = strchr(text, ':');
		if (colon) {
			*colon++ = '\0';
			char *key = trim(text);
			char *val = trim(colon);

			/* ———— 特殊节区内部的 key/value ———— */

			/* [probe] 子项处理 */
			if (current_section == SEC_PROBE) {
				/* YAML 列表项: - item */
				if (text[0] == '-' && in_probe_list) {
					char *item = trim(text + 1);
					if (strcmp(probe_list_type, "headers") == 0)
						probe_add_header(item);
					else if (strcmp(probe_list_type, "functions") == 0)
						probe_add_function(item);
					else if (strcmp(probe_list_type, "decls") == 0)
						probe_add_decl(item);
					else if (strcmp(probe_list_type, "libraries") == 0)
						probe_add_library(item);
					else if (strcmp(probe_list_type, "codes") == 0) {
						char *code_body = strchr(item, ':');
						if (code_body) {
							*code_body++ = '\0';
							probe_add_code(trim(item), trim(code_body));
						}
					} else if (strcmp(probe_list_type, "typesizes") == 0) {
						char *type_name = strchr(item, ':');
						if (type_name) {
							*type_name++ = '\0';
							probe_add_typesize(trim(item), trim(type_name));
						}
					}
					line = end;
					continue;
				}
				/* 探测节区 key: value */
				in_probe_list = 0;
				if (strcmp(key, "cc") == 0) { probe_set_cc(val); }
				else if (strcmp(key, "cflags") == 0) { probe_set_cflags(val); }
				else if (strcmp(key, "config") == 0) { probe_set_config(trim(val)); }
				else if (strcmp(key, "headers") == 0) {
					in_probe_list = 1; strcpy(probe_list_type, "headers");
					/* 也可以用逗号分隔一行内指定: headers: stdio.h, stdlib.h */
					char *p = val;
					while (*p) {
						while (*p == ' ' || *p == ',') p++;
						if (!*p) break;
						char *start = p;
						while (*p && *p != ',') p++;
						char saved = *p; *p = '\0';
						probe_add_header(trim(start));
						*p = saved;
					}
				}
				else if (strcmp(key, "functions") == 0) {
					in_probe_list = 1; strcpy(probe_list_type, "functions");
					char *p = val;
					while (*p) {
						while (*p == ' ' || *p == ',') p++;
						if (!*p) break;
						char *start = p;
						while (*p && *p != ',') p++;
						char saved = *p; *p = '\0';
						probe_add_function(trim(start));
						*p = saved;
					}
				}
				else if (strcmp(key, "decls") == 0) { in_probe_list = 1; strcpy(probe_list_type, "decls"); }
				else if (strcmp(key, "libraries") == 0) { in_probe_list = 1; strcpy(probe_list_type, "libraries"); }
				else if (strcmp(key, "codes") == 0) { in_probe_list = 1; strcpy(probe_list_type, "codes"); }
				else if (strcmp(key, "typesizes") == 0) { in_probe_list = 1; strcpy(probe_list_type, "typesizes"); }
				line = end;
				continue;
			}

			/* [variables] / [env] 节区 */
			if (current_section == SEC_VARIABLES) {
				set_env(key, val);
				line = end;
				continue;
			}

			/* [depends] 节区 */
			if (current_section == SEC_DEPENDS) {
				if (text[0] == '-' || text[0] == '*') {
					char *pkg = trim(text + 1);
					if (nrecipe_deps < RECIPE_DEPS_MAX) {
						snprintf(recipe_deps[nrecipe_deps], sizeof recipe_deps[0], "%s", pkg);
						nrecipe_deps++;
					}
				}
				line = end;
				continue;
			}

			/* ———— 普通 target 节区 ———— */

			/* deps: a, b, c — parse as comma-separated target deps */
			if (strcmp(key, "deps") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p;
					*p = '\0';
					char *depname = trim(start);
					if (current_target->ndeps < TARGET_DEPS_MAX)
						current_target->deps[current_target->ndeps++] =
							strdup(depname);
					*p = saved;
				}
			}
			/* when: condition — 条件表达式 */
			else if (strcmp(key, "when") == 0 && current_target) {
				char *trimmed = val;
				while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
				if (*trimmed)
					current_target->when = strdup(trimmed);
			}
			/* inputs: / outputs: — 增量构建跟踪 */
			else if (strcmp(key, "inputs") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					char *iname = trim(start);
					if (current_target->ninputs < TARGET_DEPS_MAX)
						current_target->inputs[current_target->ninputs++] = strdup(iname);
					*p = saved;
				}
			}
			else if (strcmp(key, "outputs") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					char *oname = trim(start);
					if (current_target->noutputs < TARGET_DEPS_MAX)
						current_target->outputs[current_target->noutputs++] = strdup(oname);
					*p = saved;
				}
			}
			/* phony: true — 标记为伪目标 */
			else if (strcmp(key, "phony") == 0 && current_target) {
				if (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0 || strcmp(val, "1") == 0)
					current_target->phony = 1;
			}
			/* download: URL — 下载源文件 */
			else if (strcmp(key, "download") == 0 && current_target) {
				current_target->download_url = strdup(val);
			}
			/* has: tool1, tool2 — 检测构建工具 */
			else if (strcmp(key, "has") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					if (nhas_tools_stack < HAS_TOOLS_MAX)
						has_tools_stack[nhas_tools_stack++] = strdup(trim(start));
					*p = saved;
				}
			}
			/* lib: lib1, lib2 — 检测库依赖（自动调用 probe） */
			else if (strcmp(key, "lib") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					char *libname = trim(start);
					if (nlib_deps_stack < LIB_DEPS_MAX)
						lib_deps_stack[nlib_deps_stack++] = strdup(libname);
					probe_add_library(libname);
					*p = saved;
				}
			}
			/* log: filename — 构建日志输出文件 */
			else if (strcmp(key, "log") == 0 && current_target) {
				current_target->log_file = strdup(val);
			}
			/* toolchain: <prefix> — 交叉编译工具链前缀 */
			else if (strcmp(key, "toolchain") == 0 && current_target) {
				current_target->toolchain_prefix = strdup(val);
				char eb[256];
				snprintf(eb, sizeof(eb), "%sgcc", trim(val));
				set_env("CC", eb);
				snprintf(eb, sizeof(eb), "%sar", trim(val));
				set_env("AR", eb);
				snprintf(eb, sizeof(eb), "%sstrip", trim(val));
				set_env("STRIP", eb);
			}
			/* cflags: <flags> — CFLAGS 快捷声明 */
			else if (strcmp(key, "cflags") == 0) {
				size_t clen = strlen(cflags_global);
				if (clen) strncat(cflags_global, " ", sizeof(cflags_global) - clen - 1);
				strncat(cflags_global, val, sizeof(cflags_global) - strlen(cflags_global) - 1);
				set_env("CFLAGS", cflags_global);
			}
			/* ldflags: <flags> — LDFLAGS 快捷声明 */
			else if (strcmp(key, "ldflags") == 0) {
				size_t llen = strlen(ldflags_global);
				if (llen) strncat(ldflags_global, " ", sizeof(ldflags_global) - llen - 1);
				strncat(ldflags_global, val, sizeof(ldflags_global) - strlen(ldflags_global) - 1);
				set_env("LDFLAGS", ldflags_global);
			}
			/* srcdir: <path> — 源码目录 */
			else if (strcmp(key, "srcdir") == 0 && current_target) {
				current_target->src_dir = strdup(val);
				set_env("srcdir", val);
			}
			/* builddir: <path> — 构建输出目录 */
			else if (strcmp(key, "builddir") == 0 && current_target) {
				current_target->build_dir = strdup(val);
				set_env("builddir", val);
			}
			/* sha256: <hash> — 下载文件 SHA-256 校验 */
			else if (strcmp(key, "sha256") == 0 && current_target) {
				current_target->download_sha256 = strdup(val);
			}
			/* parallel: <N> — 每目标并行度 */
			else if (strcmp(key, "parallel") == 0 && current_target) {
				int n = atoi(val);
				if (n > 0) current_target->parallel_jobs = n;
			}
			/* unpack: true — 自动解压下载的压缩包 */
			else if (strcmp(key, "unpack") == 0 && current_target) {
				if (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0)
					current_target->run_quiet = 2;  /* flag: unpack requested */
			}
			/* patch: file1, file2 — 补丁文件列表 */
			else if (strcmp(key, "patch") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					if (current_target->npatch < TARGET_DEPS_MAX)
						current_target->patches[current_target->npatch++] = strdup(trim(start));
					*p = saved;
				}
			}
			/* test: <command> — 测试命令 */
			else if (strcmp(key, "test") == 0 && current_target) {
				current_target->test_cmd = strdup(val);
			}
			/* copy: src=path, dest=path, mode=755 — 声明式文件复制 */
			else if (strcmp(key, "copy") == 0 && current_target) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					if (strncmp(p, "src=", 4) == 0) {
						p += 4; char *end = strchr(p, ',');
						if (end) { *end = '\0'; current_target->install_src = strdup(trim(p)); *end = ','; p = end + 1; }
						else { current_target->install_src = strdup(trim(p)); break; }
					} else if (strncmp(p, "dest=", 5) == 0) {
						p += 5; char *end = strchr(p, ',');
						if (end) { *end = '\0'; current_target->install_dest = strdup(trim(p)); *end = ','; p = end + 1; }
						else { current_target->install_dest = strdup(trim(p)); break; }
					} else if (strncmp(p, "mode=", 5) == 0) {
						p += 5; current_target->install_mode = strtol(p, NULL, 8); break;
					} else break;
				}
			}
			/* strip: true — 安装后 strip 调试符号 */
			else if (strcmp(key, "strip") == 0 && current_target) {
				current_target->strip = (strcmp(val, "true") == 0 || strcmp(val, "yes") == 0);
			}
			else if (strcmp(key, "default") == 0) {
				default_target = strdup(val);
			}
			/* uses: lib1, lib2 — 库依赖 */
			else if (strcmp(key, "uses") == 0) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p;
					*p = '\0';
					char *libname = trim(start);
					if (nuses < USES_MAX && *libname) {
						uses[nuses] = strdup(libname);
						if (uses[nuses]) nuses++;
						/* 立即 setenv 以便后续 %VAR% 插值 */
						const struct pkg_lib *lib = find_lib(libname);
						if (lib) {
							char vn[128];
							snprintf(vn, sizeof(vn), "PKG_%s_LIBS", libname);
							for (char *c = vn + 4; *c; c++)
								if (*c >= 'a' && *c <= 'z') *c &= ~0x20;
							setenv(vn, lib->libs, 1);
							snprintf(vn, sizeof(vn), "PKG_%s_CFLAGS", libname);
							for (char *c = vn + 4; *c; c++)
								if (*c >= 'a' && *c <= 'z') *c &= ~0x20;
							setenv(vn, lib->cflags, 1);
						}
					}
					*p = saved;
				}
				/* 聚合 LIBS/CFLAGS */
				char agg[2048] = {0};
				for (size_t i = 0; i < nuses; i++) {
					const struct pkg_lib *lib = find_lib(uses[i]);
					if (lib && lib->libs[0]) {
						if (agg[0]) strcat(agg, " ");
						strcat(agg, lib->libs);
					}
				}
				setenv("LIBS", agg, 1);
				agg[0] = '\0';
				for (size_t i = 0; i < nuses; i++) {
					const struct pkg_lib *lib = find_lib(uses[i]);
					if (lib && lib->cflags[0]) {
						if (agg[0]) strcat(agg, " ");
						strcat(agg, lib->cflags);
					}
				}
				setenv("CFLAGS", agg, 1);
			}
			/* depends: pkg1, pkg2 — 跨包依赖 */
			else if (strcmp(key, "depends") == 0) {
				char *p = val;
				while (*p) {
					while (*p == ' ' || *p == ',') p++;
					if (!*p) break;
					char *start = p;
					while (*p && *p != ',') p++;
					char saved = *p; *p = '\0';
					if (nrecipe_deps < RECIPE_DEPS_MAX) {
						snprintf(recipe_deps[nrecipe_deps], sizeof recipe_deps[0],
						         "%s", trim(start));
						nrecipe_deps++;
					}
					*p = saved;
				}
			}
			/* env: KEY=VALUE — 环境变量注入（每行一个，支持 ''/"" 引号） */
			else if (strcmp(key, "env") == 0) {
				char *eq = strchr(val, '=');
				if (eq) {
					char *eq_save = eq;
					*eq = '\0';
					char *k = trim(val);
					char *v = trim(eq + 1);
					size_t vlen = strlen(v);
					if (vlen >= 2 && ((v[0] == '"' && v[vlen-1] == '"') ||
					                  (v[0] == '\'' && v[vlen-1] == '\''))) {
						v[vlen-1] = '\0';
						v++;
					}
					set_env(k, v);
					*eq_save = '=';
				}
			}
			/* name: / version: — 元数据，同时导出 %NAME%/%VERSION% 和 %PKG_NAME%/%PKG_VERSION% */
			else if (strcmp(key, "name") == 0 || strcmp(key, "version") == 0) {
				char interp[1024];
				interpolate(val, interp, sizeof(interp));
				/* 导出大写形式 */
				char upper[64];
				size_t kl = strlen(key);
				for (size_t ki = 0; ki < kl && ki < sizeof(upper)-1; ki++)
					upper[ki] = (key[ki] >= 'a' && key[ki] <= 'z')
					            ? key[ki] - 'a' + 'A' : key[ki];
				upper[kl] = '\0';
				set_env(upper, interp);
				/* 也导出带 PKG_ 前缀的版本 */
				char pkgvar[64];
				snprintf(pkgvar, sizeof(pkgvar), "PKG_%s", upper);
				set_env(pkgvar, interp);
			}
			/* target: — 设置构建三元组 */
			else if (strcmp(key, "target") == 0 && current_target == NULL) {
				char interp[1024];
				interpolate(val, interp, sizeof(interp));
				struct mt_triple tri;
				char tri_str[128];
				if (infer_triple(interp, &tri) == 0) {
					triple_to_string(&tri, tri_str, sizeof(tri_str));
					set_env("TARGET_TRIPLE", tri_str);
					set_env("ARCH", tri.arch);
					set_env("ABI", tri.abi);
					if (tri.subarch[0] && strcmp(tri.subarch, "baseline") != 0)
						set_env("SUBARCH", tri.subarch);
				}
			}
			/* key: + 值空 = 多行块开始 */
			else if (!*val && current_target) {
				if (strncmp(key, "run", 3) == 0) {
					/* 解析修饰符 run(!): / run(?): / run(q): */
					const char *mod = key + 3;
					if (*mod == '(') {
						mod++;
						while (*mod && *mod != ')') {
							switch (*mod) {
								case '!': current_target->run_abort_on_fail = 1; break;
								case '?': current_target->run_abort_on_fail = 0; break;
								case 'q': current_target->run_quiet = 1; break;
							}
							mod++;
						}
					}
					in_runblock = 1;
					runblock_len = 0;
					runblock_buf[0] = '\0';
				}
			}
			/* run(X): command 同行情景（支持修饰符） */
			else if (*val && strncmp(key, "run", 3) == 0 && current_target) {
				const char *mod = key + 3;
				int mod_abort = 1, mod_quiet = 0;
				if (*mod == '(') {
					mod++;
					while (*mod && *mod != ')') {
						switch (*mod) {
							case '!': mod_abort = 1; break;
							case '?': mod_abort = 0; break;
							case 'q': mod_quiet = 1; break;
						}
						mod++;
					}
				}
				current_target->run_abort_on_fail = mod_abort;
				current_target->run_quiet = mod_quiet;
				in_runblock = 0;
				flush_runblock();
				add_to_runblock(trim(val));
				flush_runblock();
			}
			/* 其他 key: value — 同时存入 recipe_environment 和 setenv */
			else {
				char interp[1024];
				interpolate(val, interp, sizeof(interp));
				set_env(key, interp);
			}
		}

		line = end;
	}
	flush_runblock();

	/* 展开 uses: 库依赖的环境变量 */
	if (nuses > 0)
		expand_uses();

	return 0;
}
