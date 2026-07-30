#ifndef MT_DISASM_H
#define MT_DISASM_H

/*
 * MeuOS Toolchain 反汇编器接口。
 *
 * 支持按架构名分派到对应后端。
 * 当前后端: x86_64, aarch64, arm, i386, loongarch64, riscv64
 *
 * objdump 调用 mt_disasm_one() 反汇编节区，传入架构名即可。
 * 调用方循环推进 offset。输出助记符 + 操作数 + 字节十六进制。
 */

#include <stddef.h>
#include <stdint.h>

#define MT_DISASM_MNEM_CAP 16
#define MT_DISASM_OPS_CAP 160
#define MT_DISASM_BYTES_CAP 48  /* 最多 15 字节指令 -> "xx " * 15 + NUL */

struct mt_disasm_insn {
	uint64_t address;          /* 指令起始虚拟地址 */
	size_t offset;             /* 在输入 buffer 中的偏移 */
	size_t length;             /* 指令字节长度 */
	char mnemonic[MT_DISASM_MNEM_CAP];  /* 助记符 (如 "mov", "jmp") */
	char operands[MT_DISASM_OPS_CAP];   /* 操作数 (如 "%rax, %rbx") */
	char bytes_hex[MT_DISASM_BYTES_CAP]; /* 指令字节十六进制 "xx xx ..." */
};

/* 通用反汇编入口：按架构名分派到对应后端。
 *
 * arch: 架构名称，如 "x86_64"
 * bytes: 指令流起始
 * size: buffer 总大小
 * offset: 起始偏移（从 bytes 起算）
 * addr: 该偏移对应的虚拟地址（用于 PC 相对寻址显示为 0x... <symbol>）
 * out: 输出结构（调用方分配）
 *
 * 返回 0 成功，-1 失败（无法解码、buffer 越界或不支持的架构）。
 */
int mt_disasm_one(const char *arch, const unsigned char *bytes, size_t size,
                  size_t offset, uint64_t addr,
                  struct mt_disasm_insn *out);

/* 反汇编一条 x86_64 指令（架构特定后端，也可直接调用）。
 *
 * bytes: 指令流起始
 * size: buffer 总大小
 * offset: 起始偏移（从 bytes 起算）
 * addr: 该偏移对应的虚拟地址
 * out: 输出结构（调用方分配）
 *
 * 返回 0 成���，-1 失败（无法解码或 buffer 越界）。
 * 成功时 out->length 为指令字节数，out->bytes_hex/mnemonic/operands 已填充。
 * 失败时 out->length 至少为 1，mnemonic 为 "(bad)"。
 */
int mt_disasm_x86_64_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);

int mt_disasm_aarch64_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);
int mt_disasm_arm_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);
int mt_disasm_i386_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);
int mt_disasm_loongarch64_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);
int mt_disasm_riscv64_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out);

#endif
