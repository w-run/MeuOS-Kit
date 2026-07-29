/* triple.c — MeuOS 统一 triple 解析与推断
 *
 * 解析 <arch>[-<subarch>][-<vendor>][-<os>][-<abi>] 格式，
 * 并为缺失字段提供架构特定默认值。
 */
#include "triple.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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

/* ---- 解析 ---- */

int
parse_triple(const char *triplet, struct mt_triple *out)
{
	if (!triplet || !*triplet || !out)
		return -1;
	memset(out, 0, sizeof(*out));

	char buf[256];
	size_t n = 0;
	for (const char *p = triplet; *p && n < sizeof(buf) - 1; p++) {
		if (*p == '-') buf[n++] = '\0';
		else buf[n++] = *p;
	}
	buf[n] = '\0';

	char *toks[8];
	int ntok = 0;
	size_t i = 0;
	while (i < n && ntok < 8) {
		toks[ntok++] = &buf[i];
		i += strlen(&buf[i]) + 1;
	}
	if (ntok == 0) return -1;
	if (!is_known_arch(toks[0])) return -1;

	snprintf(out->arch, sizeof(out->arch), "%s", canonical_arch(toks[0]));

	for (int k = 1; k < ntok; k++) {
		const char *t = toks[k];
		if ((t[0] == 'v' && t[1] >= '0' && t[1] <= '9') ||
		    strncmp(t, "armv", 4) == 0 ||
		    strncmp(t, "rv64", 4) == 0) {
			snprintf(out->subarch, sizeof(out->subarch), "%s", t);
		} else if (strcmp(t, "meuos") == 0 || strcmp(t, "pc") == 0 ||
		           strcmp(t, "unknown") == 0) {
			snprintf(out->vendor, sizeof(out->vendor), "%s", t);
		} else if (strcmp(t, "linux") == 0 || strcmp(t, "meuos-next") == 0 ||
		           strcmp(t, "none") == 0) {
			snprintf(out->os, sizeof(out->os), "%s", t);
		} else if (strcmp(t, "gnu") == 0 || strcmp(t, "gnueabihf") == 0 ||
		           strncmp(t, "lp64", 4) == 0 || strncmp(t, "ilp32", 5) == 0) {
			snprintf(out->abi, sizeof(out->abi), "%s", t);
		} else {
			snprintf(out->subarch, sizeof(out->subarch), "%s", t);
		}
	}
	return 0;
}

/* ---- 旧接口兼容 ---- */

const char *
parse_triple_arch(const char *triplet)
{
	static char buf[32];
	if (!triplet || !*triplet) return NULL;
	struct mt_triple t;
	if (parse_triple(triplet, &t) != 0) return NULL;
	snprintf(buf, sizeof(buf), "%s", t.arch);
	return buf;
}

const char *
parse_triple_subarch(const char *triplet)
{
	static char buf[32];
	buf[0] = '\0';
	if (!triplet || !*triplet) return buf;
	struct mt_triple t;
	if (parse_triple(triplet, &t) != 0) return buf;
	snprintf(buf, sizeof(buf), "%s", t.subarch);
	return buf;
}

/* ---- triple 格式化 ---- */

char *
triple_to_string(const struct mt_triple *t, char *buf, size_t bufsz)
{
	snprintf(buf, bufsz, "%s-%s-%s-%s-%s",
	         t->arch, t->subarch, t->vendor, t->os, t->abi);
	return buf;
}

/* ---- 自动补全 ---- */

static void
arch_defaults(const char *arch, struct mt_triple *out)
{
	/* 默认 vendor/os/abi */
	snprintf(out->vendor, sizeof(out->vendor), "%s", "meuos");
	snprintf(out->os, sizeof(out->os), "%s", "linux");

	if (strcmp(arch, "x86_64") == 0) {
		snprintf(out->subarch, sizeof(out->subarch), "%s", "baseline");
		snprintf(out->abi, sizeof(out->abi), "%s", "lp64");
	} else if (strcmp(arch, "aarch64") == 0) {
		snprintf(out->subarch, sizeof(out->subarch), "%s", "armv8-a");
		snprintf(out->abi, sizeof(out->abi), "%s", "lp64");
	} else if (strcmp(arch, "riscv64") == 0) {
		snprintf(out->subarch, sizeof(out->subarch), "%s", "rv64gc");
		snprintf(out->abi, sizeof(out->abi), "%s", "lp64d");
	} else if (strcmp(arch, "loongarch64") == 0) {
		snprintf(out->subarch, sizeof(out->subarch), "%s", "baseline");
		snprintf(out->abi, sizeof(out->abi), "%s", "lp64");
	} else if (strcmp(arch, "arm") == 0) {
		snprintf(out->subarch, sizeof(out->subarch), "%s", "armv7-a");
		snprintf(out->abi, sizeof(out->abi), "%s", "gnueabihf");
	} else if (strncmp(arch, "i386", 4) == 0 || strncmp(arch, "i486", 4) == 0 ||
	           strncmp(arch, "i586", 4) == 0 || strncmp(arch, "i686", 4) == 0) {
		snprintf(out->arch, sizeof(out->arch), "%s", "i386");
		snprintf(out->subarch, sizeof(out->subarch), "%s", "baseline");
		snprintf(out->abi, sizeof(out->abi), "%s", "ilp32");
	}
}

int
infer_triple(const char *partial, struct mt_triple *out)
{
	if (!partial || !*partial) return -1;
	struct mt_triple parsed;
	if (parse_triple(partial, &parsed) != 0)
		return -1;

	memset(out, 0, sizeof(*out));
	/* 拷贝已解析的字段 */
	snprintf(out->arch, sizeof(out->arch), "%s", parsed.arch);

	/* 获取架构缺省值 */
	struct mt_triple defaults;
	memset(&defaults, 0, sizeof(defaults));
	arch_defaults(parsed.arch, &defaults);

	/* 用解析值覆盖，缺省值填空 */
	snprintf(out->subarch, sizeof(out->subarch), "%s",
	         parsed.subarch[0] ? parsed.subarch : defaults.subarch);
	snprintf(out->vendor, sizeof(out->vendor), "%s",
	         parsed.vendor[0] ? parsed.vendor : defaults.vendor);
	snprintf(out->os, sizeof(out->os), "%s",
	         parsed.os[0] ? parsed.os : defaults.os);
	snprintf(out->abi, sizeof(out->abi), "%s",
	         parsed.abi[0] ? parsed.abi : defaults.abi);

	return 0;
}
