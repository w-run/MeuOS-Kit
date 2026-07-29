/* meow - triple 解析辅助
 *
 * 统一 triple 格式：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
 * 与 mcc triple.c 保持相同逻辑，确保 --target= 跨工具解析一致。 */
#ifndef MEOW_TRIPLE_H
#define MEOW_TRIPLE_H

#include <stddef.h>

#define MT_TRIPLE_FIELD_MAX 32

struct mt_triple {
	char arch[MT_TRIPLE_FIELD_MAX];
	char subarch[MT_TRIPLE_FIELD_MAX];
	char vendor[MT_TRIPLE_FIELD_MAX];
	char os[MT_TRIPLE_FIELD_MAX];
	char abi[MT_TRIPLE_FIELD_MAX];
};

/* 解析 triple 到结构化字段。返回 0 成功，-1 无效。 */
int parse_triple(const char *triplet, struct mt_triple *out);

/* 提取架构名（兼容旧代码） */
const char *parse_triple_arch(const char *triplet);
const char *parse_triple_subarch(const char *triplet);

/* 补全三元组 + 格式化为字符串 */
int infer_triple(const char *partial, struct mt_triple *out);
char *triple_to_string(const struct mt_triple *t, char *buf, size_t bufsz);

#endif
