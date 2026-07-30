/* triple.c - MeuOS 统一 triple 解析实现
 *
 * 解析 <arch>[-<subarch>][-<vendor>][-<os>][-<abi>] 格式。
 * 字段以 '-' 分隔，空字段（连续 '-' 或尾部 '-'）视为未指定。
 */
#include "triple.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* 已知架构前缀表（用于从 triple 首段识别架构）。 */
static const char *known_arch[] = {
	"x86_64", "amd64",
	"aarch64", "arm64",
	"riscv64", "rv64",
	"loongarch64", "la64",
	"i386", "i486", "i586", "i686",
	"arm",
	NULL
};

static int
is_arch(const char *tok)
{
	for (int i = 0; known_arch[i]; i++)
		if (strcmp(tok, known_arch[i]) == 0)
			return 1;
	return 0;
}

/* 返回规范化 arch 名（用于匹配 backend）。 */
static const char *
canon_arch(const char *tok)
{
	if (strcmp(tok, "amd64") == 0) return "x86_64";
	if (strcmp(tok, "arm64") == 0) return "aarch64";
	if (strcmp(tok, "rv64") == 0)  return "riscv64";
	if (strcmp(tok, "la64") == 0)  return "loongarch64";
	return tok;
}

int
parse_triple(const char *triplet, struct mt_triple *out)
{
	if (!triplet || !*triplet || !out)
		return -1;
	memset(out, 0, sizeof(*out));

	/* 复制并分词（破坏副本，仅本函数内使用）。 */
	char buf[256];
	size_t n = 0;
	for (const char *p = triplet; *p && n < sizeof(buf) - 1; p++) {
		if (*p == '-') buf[n++] = '\0';
		else buf[n++] = *p;
	}
	buf[n] = '\0';

	/* 分词：buf 中的段以 \0 分隔，记录每段起始。 */
	char *toks[8];
	int ntok = 0;
	size_t i = 0;
	while (i < n && ntok < 8) {
		toks[ntok++] = &buf[i];
		i += strlen(&buf[i]) + 1;
	}
	if (ntok == 0)
		return -1;

	/* 首段必须是架构。 */
	if (!is_arch(toks[0]))
		return -1;
	snprintf(out->arch, sizeof(out->arch), "%s", canon_arch(toks[0]));

	/* 后续段按位置/已知值分类。 */
	for (int k = 1; k < ntok; k++) {
		const char *t = toks[k];
		/* subarch: v2/v3/v4 或 armv* 或 rv64* 扩展串 */
		if ((t[0] == 'v' && (t[1] >= '0' && t[1] <= '9')) ||
		    strncmp(t, "armv", 4) == 0 ||
		    strncmp(t, "rv64", 4) == 0) {
			snprintf(out->subarch, sizeof(out->subarch), "%s", t);
			continue;
		}
		/* vendor: meuos/pc/unknown */
		if (strcmp(t, "meuos") == 0 || strcmp(t, "pc") == 0 ||
		    strcmp(t, "unknown") == 0) {
			snprintf(out->vendor, sizeof(out->vendor), "%s", t);
			continue;
		}
		/* os: linux/meuos-next/none */
		if (strcmp(t, "linux") == 0 || strcmp(t, "meuos-next") == 0 ||
		    strcmp(t, "none") == 0) {
			snprintf(out->os, sizeof(out->os), "%s", t);
			continue;
		}
		/* abi: gnu/gnueabihf/lp64d/lp64/ilp32/ilp32d */
		if (strcmp(t, "gnu") == 0 || strcmp(t, "gnueabihf") == 0 ||
		    strncmp(t, "lp64", 4) == 0 || strncmp(t, "ilp32", 5) == 0) {
			snprintf(out->abi, sizeof(out->abi), "%s", t);
			continue;
		}
		/* 未知字段：归入 subarch（容忍未知扩展标识）。 */
		snprintf(out->subarch, sizeof(out->subarch), "%s", t);
	}
	return 0;
}
