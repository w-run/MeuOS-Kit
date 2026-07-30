/* dispatch.c - 反汇编器架构分派层。
 *
 * 根据架构名字符串将 mt_disasm_one() 分派到对应的特定架构后端。
 * 当前支持: x86_64, aarch64, arm, i386, loongarch64, riscv64
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
	else if (strcmp(arch, "aarch64") == 0)
		return mt_disasm_aarch64_one(bytes, size, offset, addr, out);
	else if (strcmp(arch, "arm") == 0)
		return mt_disasm_arm_one(bytes, size, offset, addr, out);
	else if (strcmp(arch, "i386") == 0)
		return mt_disasm_i386_one(bytes, size, offset, addr, out);
	else if (strcmp(arch, "loongarch64") == 0)
		return mt_disasm_loongarch64_one(bytes, size, offset, addr, out);
	else if (strcmp(arch, "riscv64") == 0)
		return mt_disasm_riscv64_one(bytes, size, offset, addr, out);

	/* 不支持的架构：标记为 bad 并返回 1 字节长度保持线性扫描同步。 */
	out->address = addr;
	out->offset = offset;
	out->length = 1;
	out->operands[0] = '\0';
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	out->bytes_hex[0] = '\0';
	return -1;
}
