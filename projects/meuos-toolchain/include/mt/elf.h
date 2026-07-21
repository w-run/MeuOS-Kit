#ifndef MT_ELF_H
#define MT_ELF_H

/*
 * MeuOS Toolchain 的最小 ELF64 读取接口。
 *
 * 这里使用显式的小端读取，不依赖宿主 <elf.h>。这样后续 as/ld 可以
 * 在 MeuOS libc 环境中使用同一套格式定义，而不会把宿主 libc 的布局
 * 或扩展宏泄漏进工具链源码。
 */

#include <stddef.h>
#include <stdint.h>

#define MT_ELF_NIDENT 16
#define MT_ELF64_EHDR_SIZE 64
#define MT_ELF64_PHDR_SIZE 56
#define MT_ELF64_SHDR_SIZE 64

#define MT_ELFCLASS64 2
#define MT_ELFDATA2LSB 1
#define MT_EV_CURRENT 1

#define MT_ET_REL 1
#define MT_ET_EXEC 2
#define MT_ET_DYN 3

#define MT_EM_386 3
#define MT_EM_X86_64 62
#define MT_EM_AARCH64 183
#define MT_EM_RISCV 243
#define MT_EM_LOONGARCH 258

enum mt_elf_status {
	MT_ELF_OK = 0,
	MT_ELF_E_ARGUMENT = 1,
	MT_ELF_E_TRUNCATED,
	MT_ELF_E_MAGIC,
	MT_ELF_E_CLASS,
	MT_ELF_E_ENCODING,
	MT_ELF_E_VERSION,
	MT_ELF_E_LAYOUT,
	MT_ELF_E_UNSUPPORTED
};

struct mt_elf64_view {
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint64_t entry;
	uint64_t program_offset;
	uint64_t section_offset;
	uint32_t flags;
	uint16_t header_size;
	uint16_t program_entry_size;
	uint16_t program_count;
	uint16_t section_entry_size;
	uint16_t section_count;
	uint16_t section_name_index;
};

/* 只读取并验证 ELF64 little-endian 文件头及其表范围。 */
enum mt_elf_status mt_elf64_parse(const void *bytes, size_t size,
                                   struct mt_elf64_view *view);

const char *mt_elf_status_string(enum mt_elf_status status);
const char *mt_elf_machine_name(uint16_t machine);

#endif
