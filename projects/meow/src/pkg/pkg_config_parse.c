/* pkg_config_parse.c — .pc 文件解析器
 *
 * 解析标准的 pkg-config .pc 文件格式，支持变量展开和 Requires: 递归。
 * 为 meow pkg-config 子命令和 uses: 指令提供 .pc 文件支持。
 *
 * 搜索路径优先级:
 *   1. $PKG_CONFIG_PATH 环境变量（冒号分隔）
 *   2. /usr/lib/pkgconfig
 *   3. /usr/lib/x86_64-linux-gnu/pkgconfig （Debian 多架构路径）
 *   4. /usr/share/pkgconfig */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "meow.h"

/* 最大变量数和路径搜索数 */
#define PC_VARS_MAX 64
#define PC_SEARCH_PATHS_MAX 16

/* 单个变量定义 */
struct pc_var {
	char key[256];
	char value[1024];
};

/* 被解析的 .pc 文件内容 */
struct pc_file {
	char libs[4096];
	char cflags[4096];
	char requires[4096];
	char requires_private[4096];
	int  nvars;
	struct pc_var vars[PC_VARS_MAX];
	char path[1024];       /* 文件路径 */
	char name[256];
	char description[256];
	char version[64];
};

/* ---- 内部工具函数 ---- */

/* 去除行首尾空白，返回去掉空白后的起始指针 */
static char *
trim_line(char *line)
{
	while (*line == ' ' || *line == '\t')
		line++;
	char *end = line + strlen(line);
	while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		end--;
	*end = '\0';
	return line;
}

/* 展开字符串中的 ${var} 引用
 * vars 中的值必须是已展开的（或会被递归展开直到稳定）*/
static int
expand_vars_in_place(char *buf, size_t buf_sz, struct pc_var *vars, int nvars)
{
	char result[4096];
	size_t rpos = 0;
	char *src = buf;

	while (*src && rpos < sizeof(result) - 1) {
		if (*src == '$' && *(src + 1) == '{') {
			/* 找到变量名 */
			src += 2; /* 跳过 ${ */
			char varname[256];
			int vi = 0;
			while (*src && *src != '}' && vi < (int)sizeof(varname) - 1)
				varname[vi++] = *src++;
			varname[vi] = '\0';
			if (*src == '}')
				src++; /* 跳过 } */

			/* 查找变量值 */
			const char *val = "";
			for (int i = 0; i < nvars; i++) {
				if (strcmp(vars[i].key, varname) == 0) {
					val = vars[i].value;
					break;
				}
			}

			/* 追加变量值到结果 */
			while (*val && rpos < sizeof(result) - 1)
				result[rpos++] = *val++;
		} else {
			result[rpos++] = *src++;
		}
	}
	result[rpos] = '\0';

	/* 写回原缓冲区 */
	if (rpos < buf_sz) {
		memcpy(buf, result, rpos + 1);
		return 0;
	}
	return -1;
}

/* ---- 搜索路径 ---- */

/* 获取 .pc 文件搜索路径列表 */
static int
get_search_paths(char *paths[], int max_paths)
{
	int np = 0;
	char *env = getenv("PKG_CONFIG_PATH");
	if (env && *env) {
		char *dup = strdup(env);
		if (dup) {
			char *save;
			char *tok = strtok_r(dup, ":", &save);
			while (tok && np < max_paths) {
				if (*tok)
					paths[np++] = tok;
				tok = strtok_r(NULL, ":", &save);
			}
		}
		/* 注意：内存泄漏可控，此函数仅被调用几次 */
	}

	/* 默认路径 */
	static const char *default_paths[] = {
		"/usr/lib/pkgconfig",
		"/usr/lib/x86_64-linux-gnu/pkgconfig",
		"/usr/share/pkgconfig",
		NULL
	};

	for (int i = 0; default_paths[i] && np < max_paths; i++)
		paths[np++] = (char *)default_paths[i];

	return np;
}

/* 查找 .pc 文件，返回 0 表示找到，-1 表示未找到 */
int
pkg_config_find(const char *name, char *path, size_t path_sz)
{
	char *search_paths[PC_SEARCH_PATHS_MAX];
	int np = get_search_paths(search_paths, PC_SEARCH_PATHS_MAX);

	for (int i = 0; i < np; i++) {
		char candidate[1024];
		snprintf(candidate, sizeof(candidate), "%s/%s.pc", search_paths[i], name);

		FILE *f = fopen(candidate, "r");
		if (f) {
			fclose(f);
			snprintf(path, path_sz, "%s", candidate);
			return 0;
		}
	}

	return -1; /* 未找到 */
}

/* ---- 文件解析 ---- */

/* 对变量值做多轮展开，直到不再包含 ${...} 引用 */
static void
expand_all_vars(struct pc_var *vars, int nvars)
{
	for (int iter = 0; iter < 10; iter++) {
		int changed = 0;
		for (int i = 0; i < nvars; i++) {
			char before[1024];
			snprintf(before, sizeof(before), "%s", vars[i].value);
			expand_vars_in_place(vars[i].value, sizeof(vars[i].value),
			                     vars, nvars);
			if (strcmp(before, vars[i].value) != 0)
				changed = 1;
		}
		if (!changed)
			break;  /* 展开已稳定 */
	}
}

/* 解析完整的 .pc 文件
 *
 * 前处理：
 *   在第一个空行之前的所有 "key=value" 行被认为是变量定义
 *   之后是字段行，格式为 "FieldName: value"
 *
 * 支持的关键字段:
 *   Name:, Description:, Version:, Requires:, Libs:, Cflags: */
int
pkg_config_parse(const char *path, struct pc_file *out)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	memset(out, 0, sizeof(*out));
	snprintf(out->path, sizeof(out->path), "%s", path);

	char line[4096];
	int in_vars = 1;    /* 仍在变量定义区（空行之前）*/
	int saw_blank = 0;  /* 是否已遇到空行 */

	while (fgets(line, sizeof(line), f)) {
		char *trimmed = trim_line(line);

		/* 跳过空行和注释行 */
		if (*trimmed == '\0' || *trimmed == '#') {
			if (*trimmed == '\0' && !saw_blank) {
				saw_blank = 1;
				in_vars = 0;  /* 空行后进入字段区 */
				/* 变量收集完毕，展开所有变量引用 */
				expand_all_vars(out->vars, out->nvars);
			}
			continue;
		}

		if (in_vars && strchr(trimmed, '=')) {
			/* 变量定义行 */
			char *eq = strchr(trimmed, '=');
			*eq = '\0';
			char *k = trim_line(trimmed);
			char *v = trim_line(eq + 1);

			if (out->nvars < PC_VARS_MAX) {
				snprintf(out->vars[out->nvars].key, sizeof(out->vars[out->nvars].key), "%s", k);
				snprintf(out->vars[out->nvars].value, sizeof(out->vars[out->nvars].value), "%s", v);
				out->nvars++;
			}
			continue;
		}

		/* 字段行：FieldName: value */
		char *colon = strchr(trimmed, ':');
		if (!colon)
			continue;

		*colon = '\0';
		char *field = trim_line(trimmed);
		char *value = trim_line(colon + 1);

		/* 展开值中的变量引用 */
		char expanded[4096];
		snprintf(expanded, sizeof(expanded), "%s", value);
		expand_vars_in_place(expanded, sizeof(expanded), out->vars, out->nvars);

		if (strcmp(field, "Name") == 0) {
			snprintf(out->name, sizeof(out->name), "%s", expanded);
		} else if (strcmp(field, "Description") == 0) {
			snprintf(out->description, sizeof(out->description), "%s", expanded);
		} else if (strcmp(field, "Version") == 0) {
			snprintf(out->version, sizeof(out->version), "%s", expanded);
		} else if (strcmp(field, "Requires") == 0) {
			/* 追加到 requires，逗号分隔视为空格分隔 */
			if (out->requires[0])
				strncat(out->requires, " ", sizeof(out->requires) - strlen(out->requires) - 1);
			/* 替换逗号为空格 */
			char req_buf[4096];
			snprintf(req_buf, sizeof(req_buf), "%s", expanded);
			for (char *p = req_buf; *p; p++)
				if (*p == ',') *p = ' ';
			strncat(out->requires, req_buf,
			        sizeof(out->requires) - strlen(out->requires) - 1);
		} else if (strcmp(field, "Requires.private") == 0) {
			if (out->requires_private[0])
				strncat(out->requires_private, " ", sizeof(out->requires_private) - strlen(out->requires_private) - 1);
			char req_buf[4096];
			snprintf(req_buf, sizeof(req_buf), "%s", expanded);
			for (char *p = req_buf; *p; p++)
				if (*p == ',') *p = ' ';
			strncat(out->requires_private, req_buf,
			        sizeof(out->requires_private) - strlen(out->requires_private) - 1);
		} else if (strcmp(field, "Libs") == 0) {
			if (out->libs[0])
				strncat(out->libs, " ", sizeof(out->libs) - strlen(out->libs) - 1);
			strncat(out->libs, expanded, sizeof(out->libs) - strlen(out->libs) - 1);
		} else if (strcmp(field, "Libs.private") == 0) {
			/* Libs.private 不追加到公共 flags，
			 * 只有 --static 时才需要 */
		} else if (strcmp(field, "Cflags") == 0) {
			if (out->cflags[0])
				strncat(out->cflags, " ", sizeof(out->cflags) - strlen(out->cflags) - 1);
			strncat(out->cflags, expanded, sizeof(out->cflags) - strlen(out->cflags) - 1);
		}
		/* 其他字段（如 URL, conflicts）忽略 */
	}

	fclose(f);
	return 0;
}

/* ---- 高层查询 API ---- */

/* 递归解析一个包的 Requires */
static int
resolve_requires(const char *requires_str, char *cflags, size_t cflags_sz,
                 char *libs, size_t libs_sz, int depth);

/* 查询单个包（通过 .pc 或 known_libs 回退）*/
static int
query_single(const char *name, char *cflags, size_t cflags_sz,
             char *libs, size_t libs_sz, int depth)
{
	if (depth > 8) {
		fprintf(stderr, "meow pkg-config: circular dependency detected for '%s'\n", name);
		return -1;
	}

	char path[1024];

	/* 优先尝试 .pc 文件 */
	if (pkg_config_find(name, path, sizeof(path)) == 0) {
		struct pc_file pc;
		if (pkg_config_parse(path, &pc) == 0) {
			/* 追加本包的 flags */
			if (pc.libs[0]) {
				if (libs[0])
					strncat(libs, " ", libs_sz - strlen(libs) - 1);
				strncat(libs, pc.libs, libs_sz - strlen(libs) - 1);
			}
			if (pc.cflags[0]) {
				if (cflags[0])
					strncat(cflags, " ", cflags_sz - strlen(cflags) - 1);
				strncat(cflags, pc.cflags, cflags_sz - strlen(cflags) - 1);
			}

			/* 递归解析 Requires */
			if (pc.requires[0]) {
				resolve_requires(pc.requires, cflags, cflags_sz,
				                 libs, libs_sz, depth + 1);
			}
			return 0;
		}
	}

	/* 回退到 known_libs 表 */
	const struct pkg_lib *lib = find_lib(name);
	if (lib) {
		if (lib->libs[0]) {
			if (libs[0])
				strncat(libs, " ", libs_sz - strlen(libs) - 1);
			strncat(libs, lib->libs, libs_sz - strlen(libs) - 1);
		}
		if (lib->cflags[0]) {
			if (cflags[0])
				strncat(cflags, " ", cflags_sz - strlen(cflags) - 1);
			strncat(cflags, lib->cflags, cflags_sz - strlen(cflags) - 1);
		}
		return 0;
	}

	return -1; /* 未找到 */
}

/* 解析 Requires 字段（空格分隔的包名，可能带有版本约束如 "glib-2.0 >= 2.40"）*/
static int
resolve_requires(const char *requires_str, char *cflags, size_t cflags_sz,
                 char *libs, size_t libs_sz, int depth)
{
	char buf[4096];
	snprintf(buf, sizeof(buf), "%s", requires_str);

	char *save;
	char *tok = strtok_r(buf, " ", &save);
	while (tok) {
		/* 跳过版本比较操作符和版本号 */
		if (strcmp(tok, ">=") == 0 || strcmp(tok, ">") == 0 ||
		    strcmp(tok, "<=") == 0 || strcmp(tok, "<") == 0 ||
		    strcmp(tok, "=") == 0) {
			tok = strtok_r(NULL, " ", &save);
			if (tok) tok = strtok_r(NULL, " ", &save);
			continue;
		}

		/* 跳过看起来是版本号的 token（以数字开头或含 .）*/
		if (tok[0] >= '0' && tok[0] <= '9') {
			tok = strtok_r(NULL, " ", &save);
			continue;
		}

		/* 这是一个包名 */
		query_single(tok, cflags, cflags_sz, libs, libs_sz, depth);
		tok = strtok_r(NULL, " ", &save);
	}

	return 0;
}

/* 高层查询：查找包（.pc 优先，known_libs 回退），返回 flags
 *
 * 参数:
 *   name        - 包名
 *   cflags      - 输出缓冲区，接收 Cflags（包括 Requires 的递归展开）
 *   cflags_sz   - cflags 缓冲区大小
 *   libs        - 输出缓冲区，接收 Libs
 *   libs_sz     - libs 缓冲区大小
 *
 * 返回值: 0 成功，-1 未找到 */
int
pkg_config_lookup(const char *name, char *cflags, size_t cflags_sz,
                  char *libs, size_t libs_sz)
{
	cflags[0] = '\0';
	libs[0] = '\0';
	return query_single(name, cflags, cflags_sz, libs, libs_sz, 0);
}
