/* target_select.c - map a -target triplet to the IR backend Target
 * object and to its canonical public name.
 *
 * 支持完整 MeuOS triple 格式：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
 * 例如 x86_64-v3-meuos-linux / riscv64-meuos-linux-lp64d。
 * 旧式简写（x86_64 / aarch64）仍兼容。
 *
 * 目标 triple 自动检测：当未指定 -target 时，优先使用编译时
 * MCC_DEFAULT_TARGET（由 Makefile 从 `uname -m` 推导），其次运行时
 * 通过 `uname -m` 检测，最后回退到预编译宿主架构宏。
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "driver_internal.h"
#include "triple.h"

/* 运行时检测宿主架构：通过 popen("uname -m") 获取架构名。
 * 返回 malloc'd 字符串，调用方负责 free。失败返回 NULL。 */
static char *
detect_host_arch(void)
{
	FILE *fp = popen("uname -m 2>/dev/null", "r");
	if (!fp)
		return NULL;
	char buf[64];
	if (!fgets(buf, sizeof(buf), fp)) {
		pclose(fp);
		return NULL;
	}
	pclose(fp);
	/* 去掉末尾换行 */
	size_t n = strlen(buf);
	while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
		buf[--n] = '\0';
	if (n == 0)
		return NULL;
	return strdup(buf);
}

/* 将 `uname -m` 输出映射到 MeuOS triple 的 arch 字段。 */
static const char *
arch_to_triple_arch(const char *arch)
{
	if (!arch)
		return NULL;
	if (strcmp(arch, "x86_64") == 0 || strcmp(arch, "amd64") == 0)
		return "x86_64";
	if (strcmp(arch, "aarch64") == 0 || strcmp(arch, "arm64") == 0)
		return "aarch64";
	if (strcmp(arch, "riscv64") == 0)
		return "riscv64";
	if (strcmp(arch, "loongarch64") == 0 || strcmp(arch, "loong64") == 0)
		return "loongarch64";
	if (strncmp(arch, "i386", 4) == 0 || strncmp(arch, "i486", 4) == 0 ||
	    strncmp(arch, "i586", 4) == 0 || strncmp(arch, "i686", 4) == 0)
		return "i386";
	if (strncmp(arch, "armv", 4) == 0 || strcmp(arch, "arm") == 0 ||
	    strcmp(arch, "armv7l") == 0 || strcmp(arch, "armv8l") == 0)
		return "arm";
	return NULL;
}

Target *
pick_target(const char *triplet)
{
	struct mt_triple t;
	if (!triplet || !*triplet || parse_triple(triplet, &t) != 0) {
#if defined(MCC_DEFAULT_TARGET)
		if (strcmp(MCC_DEFAULT_TARGET, "x86_64") == 0)     return &T_amd64_sysv;
		if (strcmp(MCC_DEFAULT_TARGET, "aarch64") == 0)    return &T_arm64;
		if (strcmp(MCC_DEFAULT_TARGET, "riscv64") == 0)    return &T_rv64;
		if (strcmp(MCC_DEFAULT_TARGET, "loongarch64") == 0) return &T_la64;
		if (strcmp(MCC_DEFAULT_TARGET, "i386") == 0)       return &T_i386_sysv;
		if (strcmp(MCC_DEFAULT_TARGET, "arm") == 0)      return &T_arm32;
#endif
		/* 运行时检测：通过 uname -m 识别宿主架构 */
		{
			char *host = detect_host_arch();
			const char *ta = host ? arch_to_triple_arch(host) : NULL;
			if (ta) {
				Target *result = NULL;
				if (strcmp(ta, "x86_64") == 0)     result = &T_amd64_sysv;
				if (strcmp(ta, "aarch64") == 0)    result = &T_arm64;
				if (strcmp(ta, "riscv64") == 0)    result = &T_rv64;
				if (strcmp(ta, "loongarch64") == 0) result = &T_la64;
				if (strcmp(ta, "i386") == 0)       result = &T_i386_sysv;
				if (strcmp(ta, "arm") == 0)       result = &T_arm32;
				free(host);
				if (result)
					return result;
			}
			free(host);
		}
#if defined(__x86_64__)
		return &T_amd64_sysv;
#elif defined(__aarch64__)
		return &T_arm64;
#elif defined(__riscv) && (__riscv_xlen == 64)
		return &T_rv64;
#elif defined(__loongarch_lp64)
		return &T_la64;
#endif
		return &T_amd64_sysv;
	}
	/* 按 arch 选择 backend。subarch/vendor/os/abi 由调用方（main.c）
	 * 进一步用于设置 Target.features / float-abi / sysroot 等。 */
	if (strcmp(t.arch, "x86_64") == 0)        return &T_amd64_sysv;
	if (strcmp(t.arch, "aarch64") == 0)       return &T_arm64;
	if (strcmp(t.arch, "riscv64") == 0)       return &T_rv64;
	if (strcmp(t.arch, "loongarch64") == 0)   return &T_la64;
	if (strcmp(t.arch, "arm") == 0)           return &T_arm32;
	if (strncmp(t.arch, "i386", 4) == 0)      return &T_i386_sysv;
	return &T_amd64_sysv;
}

/* Canonical targinit() name for a triplet. NULL/unknown -> NULL so
 * targinit falls back to its default (host x86_64-sysv). */
const char *
targ_name(const char *triplet)
{
	struct mt_triple t;
	if (!triplet || !*triplet || parse_triple(triplet, &t) != 0)
		return NULL;
	if (strcmp(t.arch, "x86_64") == 0)        return "x86_64-sysv";
	if (strcmp(t.arch, "aarch64") == 0)       return "aarch64";
	if (strcmp(t.arch, "riscv64") == 0)       return "riscv64";
	if (strcmp(t.arch, "loongarch64") == 0)   return "loongarch64";
	if (strcmp(t.arch, "arm") == 0)           return "arm";
	if (strncmp(t.arch, "i386", 4) == 0)      return "i386-sysv";
	return NULL;
}

/* 返回 triple 的 subarch 字段（如 "v3" / "armv7" / "rv64gc"），
 * 空字符串表示未指定。供 -march / -mcpu 推导使用。 */
const char *
targ_subarch(const char *triplet)
{
	static char buf[MT_TRIPLE_FIELD_MAX];
	struct mt_triple t;
	if (!triplet || !*triplet || parse_triple(triplet, &t) != 0)
		return "";
	snprintf(buf, sizeof(buf), "%s", t.subarch);
	return buf;
}

/* 返回 triple 的 abi 字段（如 "lp64d" / "gnueabihf"），空串=默认。 */
const char *
targ_abi(const char *triplet)
{
	static char buf[MT_TRIPLE_FIELD_MAX];
	struct mt_triple t;
	if (!triplet || !*triplet || parse_triple(triplet, &t) != 0)
		return "";
	snprintf(buf, sizeof(buf), "%s", t.abi);
	return buf;
}
