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
#include <stdio.h>

#define MT_ELF_NIDENT 16
#define MT_ELF64_EHDR_SIZE 64
#define MT_ELF64_PHDR_SIZE 56
#define MT_ELF64_SHDR_SIZE 64
#define MT_ELF64_SYM_SIZE 24
#define MT_ELF32_EHDR_SIZE 52
#define MT_ELF32_PHDR_SIZE 32
#define MT_ELF32_SHDR_SIZE 40
#define MT_ELF32_SYM_SIZE 16

#define MT_ELFCLASS32 1
#define MT_ELFCLASS64 2
#define MT_ELFDATA2LSB 1
#define MT_EV_CURRENT 1

#define MT_ET_REL 1
#define MT_ET_EXEC 2
#define MT_ET_DYN 3

#define MT_EM_ARM 40
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
#define MT_SHT_HASH 5
#define MT_SHT_DYNAMIC 6
#define MT_SHT_NOTE 7
#define MT_SHT_NOBITS 8
#define MT_SHT_REL 9
#define MT_SHT_SHLIB 10
#define MT_SHT_DYNSYM 11
#define MT_SHT_INIT_ARRAY 14
#define MT_SHT_FINI_ARRAY 15
#define MT_SHT_PREINIT_ARRAY 16
#define MT_SHT_GROUP 17
#define MT_SHT_SYMTAB_SHNDX 18
#define MT_SHT_GNU_HASH 0x6ffffff6
#define MT_SHT_GNU_VERDEF 0x6ffffffd
#define MT_SHT_GNU_VERNEED 0x6ffffffe
#define MT_SHT_GNU_VERSYM 0x6fffffff

#define MT_SHF_WRITE 0x1
#define MT_SHF_ALLOC 0x2
#define MT_SHF_EXECINSTR 0x4
#define MT_SHF_MERGE 0x10
#define MT_SHF_STRINGS 0x20
#define MT_SHF_INFO_LINK 0x40
#define MT_SHF_LINK_ORDER 0x80
#define MT_SHF_OS_NONCONFORMING 0x100
#define MT_SHF_GROUP 0x200
#define MT_SHF_TLS 0x400

#define MT_SHN_UNDEF 0
#define MT_SHN_ABS 0xfff1
#define MT_SHN_COMMON 0xfff2

#define MT_STB_LOCAL 0
#define MT_STB_GLOBAL 1
#define MT_STB_WEAK 2
#define MT_STB_GNU_UNIQUE 10
#define MT_STB_MASK 0xf0
#define MT_STB_SHIFT 4

#define MT_STT_NOTYPE 0
#define MT_STT_OBJECT 1
#define MT_STT_FUNC 2
#define MT_STT_SECTION 3
#define MT_STT_FILE 4
#define MT_STT_COMMON 5
#define MT_STT_TLS 6
#define MT_STT_GNU_IFUNC 10
#define MT_STT_MASK 0xf

#define MT_STV_DEFAULT 0
#define MT_STV_INTERNAL 1
#define MT_STV_HIDDEN 2
#define MT_STV_PROTECTED 3
#define MT_STV_MASK 3

/* Program header types */
#define MT_PT_NULL 0
#define MT_PT_LOAD 1
#define MT_PT_DYNAMIC 2
#define MT_PT_INTERP 3
#define MT_PT_NOTE 4
#define MT_PT_SHLIB 5
#define MT_PT_PHDR 6
#define MT_PT_TLS 7
#define MT_PT_GNU_EH_FRAME 0x6474e550
#define MT_PT_GNU_STACK 0x6474e551
#define MT_PT_GNU_RELRO 0x6474e552

#define MT_PF_X 0x1
#define MT_PF_W 0x2
#define MT_PF_R 0x4

/* Dynamic tags */
#define MT_DT_NULL 0
#define MT_DT_NEEDED 1
#define MT_DT_PLTRELSZ 2
#define MT_DT_PLTGOT 3
#define MT_DT_HASH 4
#define MT_DT_STRTAB 5
#define MT_DT_SYMTAB 6
#define MT_DT_RELA 7
#define MT_DT_RELASZ 8
#define MT_DT_RELAENT 9
#define MT_DT_STRSZ 10
#define MT_DT_SYMENT 11
#define MT_DT_INIT 12
#define MT_DT_FINI 13
#define MT_DT_SONAME 14
#define MT_DT_RPATH 15
#define MT_DT_SYMBOLIC 16
#define MT_DT_REL 17
#define MT_DT_RELSZ 18
#define MT_DT_RELENT 19
#define MT_DT_PLTREL 20
#define MT_DT_DEBUG 21
#define MT_DT_TEXTREL 22
#define MT_DT_JMPREL 23
#define MT_DT_BIND_NOW 24
#define MT_DT_INIT_ARRAY 25
#define MT_DT_FINI_ARRAY 26
#define MT_DT_INIT_ARRAYSZ 27
#define MT_DT_FINI_ARRAYSZ 28
#define MT_DT_FLAGS 30
#define MT_DT_GNU_HASH 0x6ffffef5
#define MT_DT_VERSYM 0x6ffffff0
#define MT_DT_VERDEF 0x6ffffffc
#define MT_DT_VERNEED 0x6ffffffe

/* x86_64 relocation types */
#define MT_R_X86_64_NONE 0
#define MT_R_X86_64_64 1
#define MT_R_X86_64_PC32 2
#define MT_R_X86_64_GOT32 3
#define MT_R_X86_64_PLT32 4
#define MT_R_X86_64_COPY 5
#define MT_R_X86_64_GLOB_DAT 6
#define MT_R_X86_64_JUMP_SLOT 7
#define MT_R_X86_64_RELATIVE 8
#define MT_R_X86_64_GOTPCREL 9
#define MT_R_X86_64_32 10
#define MT_R_X86_64_32S 11
#define MT_R_X86_64_16 12
#define MT_R_X86_64_PC16 13
#define MT_R_X86_64_8 14
#define MT_R_X86_64_PC8 15
#define MT_R_X86_64_DTPMOD64 16
#define MT_R_X86_64_DTPOFF64 17
#define MT_R_X86_64_TPOFF64 18
#define MT_R_X86_64_TLSGD 19
#define MT_R_X86_64_TLSLD 20
#define MT_R_X86_64_DTPOFF32 21
#define MT_R_X86_64_GOTTPOFF 22
#define MT_R_X86_64_TPOFF32 23
#define MT_R_X86_64_PC64 24
#define MT_R_X86_64_GOTOFF64 25
#define MT_R_X86_64_GOTPC32 26
#define MT_R_X86_64_PLT32_B 43

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

struct mt_elf64_phdr {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t vaddr;
	uint64_t paddr;
	uint64_t filesz;
	uint64_t memsz;
	uint64_t align;
};

struct mt_elf64_rela {
	uint64_t offset;
	uint64_t info;   /* symbol index (high 32) + type (low 32) */
	int64_t addend;
};

/* 辅助宏：从 r_info 提取符号索引和重定位类型。 */
#define MT_ELF64_R_SYM(i)   ((uint32_t)((i) >> 32))
#define MT_ELF64_R_TYPE(i)  ((uint32_t)((i) & 0xffffffff))
#define MT_ELF64_R_INFO(s, t) (((uint64_t)(s) << 32) | (uint32_t)(t))

/* 辅助宏：从 st_info 提取绑定和类型。 */
#define MT_ELF64_ST_BIND(i)  ((i) >> 4)
#define MT_ELF64_ST_TYPE(i)  ((i) & 0xf)
#define MT_ELF64_ST_INFO(b, t) (((b) << 4) | ((t) & 0xf))

/* ---- 读取 API ---- */

/* 验证 ELF64 little-endian 文件头及其程序/节区表范围。 */
enum mt_elf_status mt_elf64_parse(const void *bytes, size_t size,
                                   struct mt_elf64_view *view);

/* 读取一个 section header；section 指针只描述范围，不拥有输入数据。 */
enum mt_elf_status mt_elf64_get_section(const void *bytes, size_t size,
                                        const struct mt_elf64_view *view,
                                        uint16_t index,
                                        struct mt_elf64_section *section);

/* 读取一个 program header。 */
enum mt_elf_status mt_elf64_get_phdr(const void *bytes, size_t size,
                                     const struct mt_elf64_view *view,
                                     uint16_t index,
                                     struct mt_elf64_phdr *phdr);

/* 读取 symtab/dynsym 中的一个 ELF64 symbol。 */
enum mt_elf_status mt_elf64_get_symbol(const void *bytes, size_t size,
                                       const struct mt_elf64_section *table,
                                       uint64_t index,
                                       struct mt_elf64_symbol *symbol);

/* 读取 rela/rel 节区中的一个重定位条目。 */
enum mt_elf_status mt_elf64_get_rela(const void *bytes, size_t size,
                                     const struct mt_elf64_section *table,
                                     uint64_t index,
                                     struct mt_elf64_rela *rela);

/* 从一个已验证的 string table 返回 NUL 结尾字符串的只读指针。 */
enum mt_elf_status mt_elf64_get_string(const void *bytes, size_t size,
                                       const struct mt_elf64_section *strings,
                                       uint32_t offset, const char **value);

/* 按名称查找节区（使用 .shstrtab）。返回 MT_ELF_E_LAYOUT 当未找到。 */
enum mt_elf_status mt_elf64_find_section(const void *bytes, size_t size,
                                         const struct mt_elf64_view *view,
                                         const char *name,
                                         struct mt_elf64_section *section);

const char *mt_elf_status_string(enum mt_elf_status status);
const char *mt_elf_machine_name(uint16_t machine);
const char *mt_elf_section_type_name(uint32_t type);
const char *mt_elf_dt_name(uint64_t tag);
const char *mt_elf_pt_name(uint32_t type);

/* ---- 写入 API ----
 *
 * libelf 写入器构造一个完整的 ELF64 little-endian 文件。调用方按顺序
 * 添加节区，writer 在 finalize 时自动生成 ehdr、shstrtab 和 shdr 表。
 * 输出可复现（无时间戳、固定顺序）。
 *
 * 调用方负责管理 symtab/strtab 的内容——writer 不自动重建符号表，
 * strip/objcopy 直接复制原始节区字节即可。 */

struct mt_elf64_writer_section {
	const char *name;        /* 节区名（写入 shstrtab） */
	uint32_t type;           /* SHT_* */
	uint64_t flags;          /* SHF_* */
	uint64_t address;        /* sh_addr */
	const void *data;        /* 节区内容（NOBITS 时为 NULL） */
	uint64_t size;           /* 节区大小 */
	uint64_t alignment;      /* sh_addralign（0 或 1 表示无对齐） */
	uint32_t link;           /* sh_link（节区索引或 0） */
	uint32_t info;           /* sh_info */
	uint64_t entry_size;     /* sh_entsize */
};

struct mt_elf64_writer {
	uint16_t machine;        /* EM_* */
	uint16_t type;           /* ET_REL / ET_EXEC / ET_DYN */
	uint64_t entry;          /* e_entry */
	uint32_t flags;          /* e_flags */
	struct mt_elf64_writer_section *sections;
	size_t section_count;
	size_t section_capacity;
};

/* 初始化写入器。machine 为 EM_*，type 为 ET_*。 */
void mt_elf64_writer_init(struct mt_elf64_writer *w, uint16_t machine,
                          uint16_t type);

/* 释放写入器内部资源（不含节区 data，data 由调用方拥有）。 */
void mt_elf64_writer_free(struct mt_elf64_writer *w);

/* 追加一个节区。返回 0 成功，-1 失败（OOM）。节区 name 指针在
 * finalize 前必须保持有效。data 指针在 finalize 前必须保持有效。 */
int mt_elf64_writer_add_section(struct mt_elf64_writer *w,
                                const struct mt_elf64_writer_section *sec);

/* 设置入口地址（ET_EXEC/ET_DYN）。 */
void mt_elf64_writer_set_entry(struct mt_elf64_writer *w, uint64_t entry);

/* 设置 e_flags。 */
void mt_elf64_writer_set_flags(struct mt_elf64_writer *w, uint32_t flags);

/* 输出完整 ELF 到 FILE。返回 0 成功，-1 失败（I/O 错误）。 */
int mt_elf64_writer_finalize(struct mt_elf64_writer *w, FILE *out);

#endif
