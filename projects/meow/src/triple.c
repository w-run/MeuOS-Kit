/* meow triple 解析辅助 — 解析 <arch>[-<subarch>][-<vendor>][-<os>][-<abi>] 格式。
 *
 * 复用 mcc triple.c 的相同逻辑，保持跨组件一致。 */
#include "meow.h"
#include <stdio.h>
#include <string.h>

static const char *known_arch[] = {
	"x86_64", "amd64", "aarch64", "arm64",
	"riscv64", "rv64", "loongarch64", "la64",
	"i386", "i486", "i586", "i686", "arm",
	NULL
};

static int
is_known_arch(const char *tok)
{
	for (int i = 0; known_arch[i]; i++)
		if (strcmp(tok, known_arch[i]) == 0)
			return 1;
	return 0;
}

static const char *
canonical_arch(const char *tok)
{
	if (strcmp(tok, "amd64") == 0) return "x86_64";
	if (strcmp(tok, "arm64") == 0) return "aarch64";
	if (strcmp(tok, "rv64") == 0)  return "riscv64";
	if (strcmp(tok, "la64") == 0)  return "loongarch64";
	return tok;
}

/* 将 triple 以 '-' 分词到 tokens[]，返回字段数。 */
static int
split_triple(const char *triplet, char *buf, size_t bufsz,
             const char **tokens, int maxtok)
{
	size_t n = 0;
	for (const char *p = triplet; *p && n < bufsz - 1; p++) {
		if (*p == '-') buf[n++] = '\0';
		else buf[n++] = *p;
	}
	buf[n] = '\0';

	int nt = 0;
	size_t i = 0;
	while (i < n && nt < maxtok) {
		tokens[nt++] = &buf[i];
		i += strlen(&buf[i]) + 1;
	}
	return nt;
}

/* 解析 triple，提取架构名。不可用返回 NULL。 */
const char *
parse_triple_arch(const char *triplet)
{
	static char buf[32];
	if (!triplet || !*triplet)
		return NULL;
	char tmp[256];
	const char *toks[8];
	int nt = split_triple(triplet, tmp, sizeof(tmp), toks, 8);
	if (nt < 1 || !is_known_arch(toks[0]))
		return NULL;
	snprintf(buf, sizeof(buf), "%s", canonical_arch(toks[0]));
	return buf;
}

/* 从 triple 提取子架构（第二字段，如 v3 / armv7 / rv64gc）。空串=未指定。 */
const char *
parse_triple_subarch(const char *triplet)
{
	static char buf[32];
	buf[0] = '\0';
	if (!triplet || !*triplet)
		return buf;
	char tmp[256];
	const char *toks[8];
	int nt = split_triple(triplet, tmp, sizeof(tmp), toks, 8);
	if (nt >= 2) {
		const char *t = toks[1];
		/* subarch 需以 v* / armv* / rv64* 起始（区别于 vendor/os/abi）。 */
		if ((t[0] == 'v' && t[1] >= '0' && t[1] <= '9') ||
		    strncmp(t, "armv", 4) == 0 ||
		    strncmp(t, "rv64", 4) == 0)
			snprintf(buf, sizeof(buf), "%s", t);
	}
	return buf;
}
