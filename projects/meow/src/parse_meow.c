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

/* 当前解析状态 */

static struct target *current_target;
static int in_runblock;    /* 收集 run: 块的行 */
static char runblock_buf[32768];
static size_t runblock_len;

static void
flush_runblock(void)
{
	if (!current_target || runblock_len == 0)
		return;
	if (current_target->ncommands >= TARGET_COMMANDS_MAX)
		return;
	/* 整个 run 块作为一条命令存入（执行时写入 temp sh 脚本） */
	current_target->commands[current_target->ncommands] =
		strdup(runblock_buf);
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
			/* name: / version: — 元数据，存入环境变量 */
			else if (strcmp(key, "name") == 0 || strcmp(key, "version") == 0) {
				char envbuf[256];
				char varname[64];
				snprintf(varname, sizeof(varname), "PKG_%s", key);
				/* 存入 recipe_environment */
				size_t len = strlen(recipe_environment);
				snprintf(envbuf, sizeof(envbuf), "export %s='%s'; ",
				         varname, val);
				if (len + strlen(envbuf) < sizeof(recipe_environment))
					memcpy(recipe_environment + len, envbuf,
					       strlen(envbuf) + 1);
			}
			/* key: + 值空 = 多行块开始 */
			else if (!*val && current_target) {
				if (strcmp(key, "run") == 0) {
					in_runblock = 1;
					runblock_len = 0;
					runblock_buf[0] = '\0';
				}
			}
			/* 其他 key: value — 存入环境变量 */
			else {
				char envbuf[256];
				char varname[64];
				/* 大写 key 作为可导出的环境变量 */
				snprintf(varname, sizeof(varname), "%s", key);
				char interp[1024];
				interpolate(val, interp, sizeof(interp));
				size_t len = strlen(recipe_environment);
				snprintf(envbuf, sizeof(envbuf), "export %s='%s'; ",
				         varname, interp);
				if (len + strlen(envbuf) < sizeof(recipe_environment))
					memcpy(recipe_environment + len, envbuf,
					       strlen(envbuf) + 1);
			}
		}

		line = end;
	}
	flush_runblock();

	return 0;
}
