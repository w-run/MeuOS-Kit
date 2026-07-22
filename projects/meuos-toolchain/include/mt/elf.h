#ifndef MT_ELF_H
#define MT_ELF_H

/*
 * MeuOS Toolchain 的 ELF 读取接口。
 *
 * 所有磁盘字段都通过显式 little-endian 读取，不依赖宿主 <elf.h>；这让
 * 同一套格式代码可以在宿主 bootstrap 和 MeuOS libc 自举环境中编译。
 */

#include <stddef.h>
#include <stdint.h>

#define MT_ELF_NIDENT 16
#define MT_ELF64_EHDR_SIZE 64
#define MT_ELF64_PHDR_SIZE 56
#define MT_ELF64_SHDR_SIZE 64
#define MT_ELF64_SYM_SIZE 24

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

#define MT_SHT_NULL 0
#define MT_SHT_PROGBITS 1
#define MT_SHT_SYMTAB 2
#define MT_SHT_STRTAB 3
#define MT_SHT_RELA 4
#define MT_SHT_NOBITS 8
#define MT_SHT_DYNSYM 11

#define MT_SHN_UNDEF 0

#define MT_STB_LOCAL 0
#define MT_STB_GLOBAL 1
#define MT_STB_WEAK 2
#define MT_STB_GNU_UNIQUE 10
#define MT_STB_MASK 0xf0
#define MT_STB_SHIFT 4

#define MT_STV_DEFAULT 0
#define MT_STV_INTERNAL 1
#define MT_STV_HIDDEN 2
#define MT_STV_PROTECTED 3
#define MT_STV_MASK 3

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

struct mt_elf64_section {
	uint32_t name;
	uint32_t type;
	uint64_t flags;
	uint64_t address;
	uint64_t offset;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t alignment;
	uint64_t entry_size;
};

struct mt_elf64_symbol {
	uint32_t name;
	uint8_t info;
	uint8_t other;
	uint16_t section;
	uint64_t value;
	uint64_t size;
};

/* 验证 ELF64 little-endian 文件头及其程序/节区表范围。 */
enum mt_elf_status mt_elf64_parse(const void *bytes, size_t size,
                                   struct mt_elf64_view *view);

/* 读取一个 section header；section 指针只描述范围，不拥有输入数据。 */
enum mt_elf_status mt_elf64_get_section(const void *bytes, size_t size,
                                        const struct mt_elf64_view *view,
                                        uint16_t index,
                                        struct mt_elf64_section *section);

/* 读取 symtab/dynsym 中的一个 ELF64 symbol。 */
enum mt_elf_status mt_elf64_get_symbol(const void *bytes, size_t size,
                                       const struct mt_elf64_section *table,
                                       uint64_t index,
                                       struct mt_elf64_symbol *symbol);

/* 从一个已验证的 string table 返回 NUL 结尾字符串的只读指针。 */
enum mt_elf_status mt_elf64_get_string(const void *bytes, size_t size,
                                       const struct mt_elf64_section *strings,
                                       uint32_t offset, const char **value);

const char *mt_elf_status_string(enum mt_elf_status status);
const char *mt_elf_machine_name(uint16_t machine);

#endif
