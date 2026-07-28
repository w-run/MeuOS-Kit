/* triple.h - MeuOS 统一 triple 格式解析
 *
 * 格式：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
 * 示例：x86_64-meuos-linux / aarch64-meuos-linux / x86_64-v3-meuos-linux
 *       armv7-meuos-linux-gnueabihf / riscv64-meuos-linux-lp64d
 *
 * vendor=meuos 隐含：--specs=meuos / 使用 $MEUOS_SYSROOT / mt/as+mt/ld / libc-meuos
 * os=meuos-next 隐含：MeuOS Next 原生环境（非 Linux syscall）
 *
 * 该结构被 mcc 和 meow 共享（triple-lib 任务将提取为共享库，当前 mcc 内联）。
 */
#ifndef MCC_TRIPLE_H
#define MCC_TRIPLE_H

#include <stddef.h>

#define MT_TRIPLE_FIELD_MAX 32

struct mt_triple {
	char arch[MT_TRIPLE_FIELD_MAX];    /* 必选：x86_64/aarch64/riscv64/i386/loongarch64/arm */
	char subarch[MT_TRIPLE_FIELD_MAX]; /* 可选：v2/v3/v4/armv7/rv64gc */
	char vendor[MT_TRIPLE_FIELD_MAX];  /* 可选：meuos/unknown/pc（默认 unknown） */
	char os[MT_TRIPLE_FIELD_MAX];      /* 可选：linux/meuos-next/none（默认 linux） */
	char abi[MT_TRIPLE_FIELD_MAX];     /* 可选：gnu/gnueabihf/lp64d/lp64/ilp32 */
};

/* 解析 triple 字符串。成功返回 0，arch 字段被填充；失败（无有效 arch）返回 -1。
 * 未知的可选字段留空字符串（不影响匹配）。 */
int parse_triple(const char *triplet, struct mt_triple *out);

#endif /* MCC_TRIPLE_H */
