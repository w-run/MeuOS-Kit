/* target_select.c - map a -target triplet to the IR backend Target
 * object and to its canonical public name.
 *
 * 支持完整 MeuOS triple 格式：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
 * 例如 x86_64-v3-meuos-linux / riscv64-meuos-linux-lp64d。
 * 旧式简写（x86_64 / aarch64）仍兼容。
 */
#include <string.h>
#include <stdlib.h>
#include "driver_internal.h"
#include "triple.h"

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
