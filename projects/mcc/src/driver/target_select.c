/* target_select.c - map a -target triplet to the IR backend Target
 * object and to its canonical public name. */
#include <string.h>
#include <stdbool.h>
#include "driver_internal.h"

Target *
pick_target(const char *triplet)
{
	if (!triplet || !*triplet) {
#if defined(MCC_DEFAULT_TARGET)
		/* Compile-time hint set by Makefile (host arch). */
		if (strcmp(MCC_DEFAULT_TARGET, "x86_64") == 0)     return &T_amd64_sysv;
		if (strcmp(MCC_DEFAULT_TARGET, "aarch64") == 0)    return &T_arm64;
		if (strcmp(MCC_DEFAULT_TARGET, "riscv64") == 0)    return &T_rv64;
		if (strcmp(MCC_DEFAULT_TARGET, "loongarch64") == 0) return &T_la64;
		if (strcmp(MCC_DEFAULT_TARGET, "i386") == 0)       return &T_i386_sysv;
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
	if (strncmp(triplet, "x86_64", 6) == 0 || strncmp(triplet, "amd64", 5) == 0)
		return &T_amd64_sysv;
	if (strncmp(triplet, "aarch64", 7) == 0 || strncmp(triplet, "arm64", 5) == 0)
		return &T_arm64;
	if (strncmp(triplet, "riscv64", 7) == 0 || strncmp(triplet, "rv64", 4) == 0)
		return &T_rv64;
	if (strncmp(triplet, "loongarch64", 11) == 0 || strncmp(triplet, "la64", 4) == 0)
		return &T_la64;
	if (strncmp(triplet, "i386", 4) == 0
	|| strncmp(triplet, "i486", 4) == 0
	|| strncmp(triplet, "i586", 4) == 0
	|| strncmp(triplet, "i686", 4) == 0)
		return &T_i386_sysv;
	return &T_amd64_sysv;
}

/* Map a target triplet (e.g. "x86_64-unknown-linux") to the canonical
 * frontend target name expected by targinit() ("x86_64-sysv",
 * "aarch64", "riscv64"). NULL/unknown -> NULL so targinit falls back
 * to its default (host x86_64-sysv). */
const char *
targ_name(const char *triplet)
{
	if (!triplet || !*triplet)
		return NULL;
	if (strncmp(triplet, "x86_64", 6) == 0 || strncmp(triplet, "amd64", 5) == 0)
		return "x86_64-sysv";
	if (strncmp(triplet, "aarch64", 7) == 0 || strncmp(triplet, "arm64", 5) == 0)
		return "aarch64";
	if (strncmp(triplet, "riscv64", 7) == 0 || strncmp(triplet, "rv64", 4) == 0)
		return "riscv64";
	if (strncmp(triplet, "loongarch64", 11) == 0 || strncmp(triplet, "la64", 4) == 0)
		return "loongarch64";
	if (strncmp(triplet, "i386", 4) == 0
	|| strncmp(triplet, "i486", 4) == 0
	|| strncmp(triplet, "i586", 4) == 0
	|| strncmp(triplet, "i686", 4) == 0)
		return "i386-sysv";
	return NULL;
}
