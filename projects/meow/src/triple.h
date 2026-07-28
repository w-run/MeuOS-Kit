/* meow - triple 解析辅助
 *
 * 统一 triple 格式：<arch>[-<subarch>][-<vendor>][-<os>][-<abi>]
 * 与 mcc triple.c 保持相同逻辑，确保 --target= 跨工具解析一致。 */
#ifndef MEOW_TRIPLE_H
#define MEOW_TRIPLE_H

const char *parse_triple_arch(const char *triplet);
const char *parse_triple_subarch(const char *triplet);

#endif
