/* dispatch.c - 反汇编器架构分派层。
 *
 * 根据架构名字符串将 mt_disasm_one() 分派到对应的特定架构后端。
 * 当前支持: x86_64。扩展时在此文件添加 strcmp/else-if 分支即可。
 */
#include "mt/disasm.h"

#include <stdio.h>
#include <string.h>

int
mt_disasm_one(const char *arch, const unsigned char *bytes, size_t size,
              size_t offset, uint64_t addr,
              struct mt_disasm_insn *out)
{
	if (strcmp(arch, "x86_64") == 0)
		return mt_disasm_x86_64_one(bytes, size, offset, addr, out);

	/* 不支持的架构：标记为 bad 并返回 1 字节长度保持线性扫描同步。 */
	out->address = addr;
	out->offset = offset;
	out->length = 1;
	out->operands[0] = '\0';
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	out->bytes_hex[0] = '\0';
	return -1;
}
