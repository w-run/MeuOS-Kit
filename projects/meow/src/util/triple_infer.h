/* triple_infer.h — 三元组自动补全
 *
 * 用户写 "x86_64" 或 "riscv64-meuos-linux"，meow 自动推断
 * 缺失的 subarch/vendor/os/abi 字段。
 *
 * 推断规则:
 *   x86_64    → x86_64-baseline-meuos-linux-lp64
 *   riscv64   → riscv64-rv64gc-meuos-linux-lp64d
 *   aarch64   → aarch64-armv8-a-meuos-linux-lp64
 *   arm       → arm-armv7-a-meuos-linux-gnueabihf
 *   i386      → i386-baseline-meuos-linux-ilp32
 *   loongarch64 → loongarch64-baseline-meuos-linux-lp64
 */
#ifndef MEOW_TRIPLE_INFER_H
#define MEOW_TRIPLE_INFER_H

#include "triple.h"  /* reuse struct mt_triple from mcc */

/* 补全一个部分三元组：缺失字段填入架构特定默认值。
 * 返回 0 成功，-1 无效架构。补全结果在 out 中。 */
int infer_triple(const char *partial, struct mt_triple *out);

/* 将补全后的三元组格式化为完整字符串（如 "x86_64-baseline-meuos-linux-lp64"）。
 * buf 至少 128 字节。返回 buf。 */
char *triple_to_string(const struct mt_triple *t, char *buf, size_t bufsz);

#endif
