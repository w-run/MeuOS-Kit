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

static struct target *current_target;
static int in_runblock;    /* 收集 run: 块的行 */
static char runblock_buf[32768];
static size_t runblock_len;

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
	ntargets = 0;
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
			/* 收集 run: 块的后续缩进行 */
			if (indent >= 2) {
				add_to_runblock(text);
				line = end;
				continue;
			} else {
				flush_runblock();
				in_runblock = 0;
			}
		}

		/* [section] — 构建目标 */
		if (text[0] == '[') {
			flush_runblock();
			char *closing = strchr(text, ']');
			if (!closing) return -1;
			*closing = '\0';
			const char *tname = trim(text + 1);
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
			/* default: target — 设置默认 target */
			else if (strcmp(key, "default") == 0) {
				default_target = strdup(val);
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
				if (strcmp(key, "run") == 0) {
					in_runblock = 1;
					runblock_len = 0;
					runblock_buf[0] = '\0';
				}
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

	return 0;
}
