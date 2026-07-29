/* elf.c - MeuOS Toolchain ELF64 little-endian reader. */
#include "mt/elf.h"

#include <stdint.h>
#include <string.h>

static uint16_t
read16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint64_t
read64(const unsigned char *p)
{
	return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}

static int
range_valid(uint64_t offset, uint64_t length, size_t size)
{
	if (offset > (uint64_t)size)
		return 0;
	return length <= (uint64_t)size - offset;
}

static int
table_valid(uint64_t offset, uint16_t entry_size,
             uint16_t count, size_t size)
{
	uint64_t length;

	if (count == 0)
		return 1;
	if (entry_size == 0)
		return 0;
	if ((uint64_t)count > UINT64_MAX / (uint64_t)entry_size)
		return 0;
	length = (uint64_t)entry_size * (uint64_t)count;
	return range_valid(offset, length, size);
}

enum mt_elf_status
mt_elf64_parse(const void *bytes, size_t size, struct mt_elf64_view *view)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint16_t phentsize, phnum, shentsize, shnum;
	int is_elf32;

	if (!p || !view)
		return MT_ELF_E_ARGUMENT;
	if (size < MT_ELF64_EHDR_SIZE)
		return MT_ELF_E_TRUNCATED;
	if (memcmp(p, "\177ELF", 4) != 0)
		return MT_ELF_E_MAGIC;
	if (p[4] == MT_ELFCLASS32) {
		is_elf32 = 1;
	} else if (p[4] == MT_ELFCLASS64) {
		is_elf32 = 0;
	} else {
		return MT_ELF_E_CLASS;
	}
	if (p[5] != MT_ELFDATA2LSB)
		return MT_ELF_E_ENCODING;
	if (p[6] != MT_EV_CURRENT || read32(p + 20) != MT_EV_CURRENT)
		return MT_ELF_E_VERSION;

	if (is_elf32) {
		if (size < MT_ELF32_EHDR_SIZE)
			return MT_ELF_E_TRUNCATED;
		phentsize = read16(p + 42);
		phnum = read16(p + 44);
		shentsize = read16(p + 46);
		shnum = read16(p + 48);
		if (read16(p + 40) < MT_ELF32_EHDR_SIZE ||
		    (phnum && phentsize < MT_ELF32_PHDR_SIZE) ||
		    (shnum && shentsize < MT_ELF32_SHDR_SIZE) ||
		    !table_valid(read32(p + 28), phentsize, phnum, size) ||
		    !table_valid(read32(p + 32), shentsize, shnum, size))
			return MT_ELF_E_LAYOUT;
		memset(view, 0, sizeof(*view));
		view->type = read16(p + 16);
		view->machine = read16(p + 18);
		view->version = read32(p + 20);
		view->entry = read32(p + 24);
		view->program_offset = read32(p + 28);
		view->section_offset = read32(p + 32);
		view->flags = read32(p + 36);
		view->header_size = read16(p + 40);
		view->program_entry_size = phentsize;
		view->program_count = phnum;
		view->section_entry_size = shentsize;
		view->section_count = shnum;
		view->section_name_index = read16(p + 50);
	} else {
		phentsize = read16(p + 54);
		phnum = read16(p + 56);
		shentsize = read16(p + 58);
		shnum = read16(p + 60);
		if (read16(p + 52) < MT_ELF64_EHDR_SIZE ||
		    (phnum && phentsize < MT_ELF64_PHDR_SIZE) ||
		    (shnum && shentsize < MT_ELF64_SHDR_SIZE) ||
		    !table_valid(read64(p + 32), phentsize, phnum, size) ||
		    !table_valid(read64(p + 40), shentsize, shnum, size))
			return MT_ELF_E_LAYOUT;
		memset(view, 0, sizeof(*view));
		view->type = read16(p + 16);
		view->machine = read16(p + 18);
		view->version = read32(p + 20);
		view->entry = read64(p + 24);
		view->program_offset = read64(p + 32);
		view->section_offset = read64(p + 40);
		view->flags = read32(p + 48);
		view->header_size = read16(p + 52);
		view->program_entry_size = phentsize;
		view->program_count = phnum;
		view->section_entry_size = shentsize;
		view->section_count = shnum;
		view->section_name_index = read16(p + 62);
	}
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf64_get_section(const void *bytes, size_t size,
                     const struct mt_elf64_view *view, uint16_t index,
                     struct mt_elf64_section *section)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;
	int is_elf32;

	if (!p || !view || !section)
		return MT_ELF_E_ARGUMENT;
	if (index >= view->section_count)
		return MT_ELF_E_LAYOUT;
	is_elf32 = (view->section_entry_size <= MT_ELF32_SHDR_SIZE);
	if (!is_elf32 && view->section_entry_size < MT_ELF64_SHDR_SIZE)
		return MT_ELF_E_LAYOUT;
	if ((uint64_t)index > UINT64_MAX / view->section_entry_size)
		return MT_ELF_E_LAYOUT;
	offset = view->section_offset +
	         (uint64_t)index * view->section_entry_size;
	if (offset < view->section_offset ||
	    !range_valid(offset, is_elf32 ? MT_ELF32_SHDR_SIZE : MT_ELF64_SHDR_SIZE, size))
		return MT_ELF_E_LAYOUT;

	p += offset;
	memset(section, 0, sizeof(*section));
	section->name = read32(p + 0);
	section->type = read32(p + 4);
	if (is_elf32) {
		section->flags = read32(p + 8);
		section->address = read32(p + 12);
		section->offset = read32(p + 16);
		section->size = read32(p + 20);
		section->link = read32(p + 24);
		section->info = read32(p + 28);
		section->alignment = read32(p + 32);
		section->entry_size = read32(p + 36);
	} else {
		section->flags = read64(p + 8);
		section->address = read64(p + 16);
		section->offset = read64(p + 24);
		section->size = read64(p + 32);
	section->link = read32(p + 40);
	section->info = read32(p + 44);
		section->alignment = read64(p + 48);
		section->entry_size = read64(p + 56);
	}
	if (section->type != MT_SHT_NOBITS &&
	    !range_valid(section->offset, section->size, size))
		return MT_ELF_E_LAYOUT;
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf64_get_symbol(const void *bytes, size_t size,
                    const struct mt_elf64_section *table, uint64_t index,
                    struct mt_elf64_symbol *symbol)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;
	int is_elf32;

	if (!p || !table || !symbol)
		return MT_ELF_E_ARGUMENT;
	if (table->type != MT_SHT_SYMTAB && table->type != MT_SHT_DYNSYM)
		return MT_ELF_E_ARGUMENT;
	is_elf32 = (table->entry_size <= MT_ELF32_SYM_SIZE);
	if (!is_elf32 && (table->entry_size < MT_ELF64_SYM_SIZE ||
	    table->size % table->entry_size != 0 ||
	    index >= table->size / table->entry_size))
		return MT_ELF_E_LAYOUT;
	if (index > UINT64_MAX / table->entry_size)
		return MT_ELF_E_LAYOUT;
	offset = table->offset + index * table->entry_size;
	if (offset < table->offset || !range_valid(offset,
	    is_elf32 ? MT_ELF32_SYM_SIZE : MT_ELF64_SYM_SIZE, size))
		return MT_ELF_E_LAYOUT;

	p += offset;
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = read32(p + 0);
	if (is_elf32) {
		symbol->value = read32(p + 4);
		symbol->size = read32(p + 8);
		symbol->info = p[12];
		symbol->other = p[13];
		symbol->section = read16(p + 14);
	} else {
		symbol->info = p[4];
		symbol->other = p[5];
		symbol->section = read16(p + 6);
		symbol->value = read64(p + 8);
		symbol->size = read64(p + 16);
	}
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf64_get_string(const void *bytes, size_t size,
                    const struct mt_elf64_section *strings, uint32_t offset,
                    const char **value)
{
	const unsigned char *p = (const unsigned char *)bytes;
	const unsigned char *end;
	const unsigned char *nul;

	if (!p || !strings || !value)
		return MT_ELF_E_ARGUMENT;
	if (strings->type != MT_SHT_STRTAB || offset >= strings->size ||
	    !range_valid(strings->offset, strings->size, size))
		return MT_ELF_E_LAYOUT;
	p += strings->offset + offset;
	end = p + (strings->size - offset);
	nul = (const unsigned char *)memchr(p, '\0', (size_t)(end - p));
	if (!nul)
		return MT_ELF_E_LAYOUT;
	*value = (const char *)p;
	return MT_ELF_OK;
}

const char *
mt_elf_status_string(enum mt_elf_status status)
{
	switch (status) {
	case MT_ELF_OK: return "ok";
	case MT_ELF_E_ARGUMENT: return "invalid argument";
	case MT_ELF_E_TRUNCATED: return "truncated ELF image";
	case MT_ELF_E_MAGIC: return "not an ELF image";
	case MT_ELF_E_CLASS: return "unsupported ELF class";
	case MT_ELF_E_ENCODING: return "unsupported ELF byte order";
	case MT_ELF_E_VERSION: return "unsupported ELF version";
	case MT_ELF_E_LAYOUT: return "invalid ELF table layout";
	case MT_ELF_E_UNSUPPORTED: return "unsupported ELF feature";
	}
	return "unknown ELF error";
}

const char *
mt_elf_machine_name(uint16_t machine)
{
	switch (machine) {
	case MT_EM_386: return "i386";
	case MT_EM_X86_64: return "x86_64";
	case MT_EM_AARCH64: return "aarch64";
	case MT_EM_RISCV: return "riscv";
	case MT_EM_LOONGARCH: return "loongarch";
	default: return "unknown";
	}
}

const char *
mt_elf_section_type_name(uint32_t type)
{
	switch (type) {
	case MT_SHT_NULL: return "NULL";
	case MT_SHT_PROGBITS: return "PROGBITS";
	case MT_SHT_SYMTAB: return "SYMTAB";
	case MT_SHT_STRTAB: return "STRTAB";
	case MT_SHT_RELA: return "RELA";
	case MT_SHT_HASH: return "HASH";
	case MT_SHT_DYNAMIC: return "DYNAMIC";
	case MT_SHT_NOTE: return "NOTE";
	case MT_SHT_NOBITS: return "NOBITS";
	case MT_SHT_REL: return "REL";
	case MT_SHT_DYNSYM: return "DYNSYM";
	case MT_SHT_INIT_ARRAY: return "INIT_ARRAY";
	case MT_SHT_FINI_ARRAY: return "FINI_ARRAY";
	case MT_SHT_PREINIT_ARRAY: return "PREINIT_ARRAY";
	case MT_SHT_GROUP: return "GROUP";
	case MT_SHT_SYMTAB_SHNDX: return "SYMTAB SHNDX";
	case MT_SHT_GNU_HASH: return "GNU_HASH";
	case MT_SHT_GNU_VERDEF: return "VERDEF";
	case MT_SHT_GNU_VERNEED: return "VERNEED";
	case MT_SHT_GNU_VERSYM: return "VERSYM";
	default: return NULL;
	}
}

const char *
mt_elf_dt_name(uint64_t tag)
{
	switch (tag) {
	case MT_DT_NULL: return "NULL";
	case MT_DT_NEEDED: return "NEEDED";
	case MT_DT_PLTRELSZ: return "PLTRELSZ";
	case MT_DT_PLTGOT: return "PLTGOT";
	case MT_DT_HASH: return "HASH";
	case MT_DT_STRTAB: return "STRTAB";
	case MT_DT_SYMTAB: return "SYMTAB";
	case MT_DT_RELA: return "RELA";
	case MT_DT_RELASZ: return "RELASZ";
	case MT_DT_RELAENT: return "RELAENT";
	case MT_DT_STRSZ: return "STRSZ";
	case MT_DT_SYMENT: return "SYMENT";
	case MT_DT_INIT: return "INIT";
	case MT_DT_FINI: return "FINI";
	case MT_DT_SONAME: return "SONAME";
	case MT_DT_RPATH: return "RPATH";
	case MT_DT_SYMBOLIC: return "SYMBOLIC";
	case MT_DT_REL: return "REL";
	case MT_DT_RELSZ: return "RELSZ";
	case MT_DT_RELENT: return "RELENT";
	case MT_DT_PLTREL: return "PLTREL";
	case MT_DT_DEBUG: return "DEBUG";
	case MT_DT_TEXTREL: return "TEXTREL";
	case MT_DT_JMPREL: return "JMPREL";
	case MT_DT_BIND_NOW: return "BIND_NOW";
	case MT_DT_INIT_ARRAY: return "INIT_ARRAY";
	case MT_DT_FINI_ARRAY: return "FINI_ARRAY";
	case MT_DT_INIT_ARRAYSZ: return "INIT_ARRAYSZ";
	case MT_DT_FINI_ARRAYSZ: return "FINI_ARRAYSZ";
	case MT_DT_FLAGS: return "FLAGS";
	case MT_DT_GNU_HASH: return "GNU_HASH";
	case MT_DT_VERSYM: return "VERSYM";
	case MT_DT_VERDEF: return "VERDEF";
	case MT_DT_VERNEED: return "VERNEED";
	default: return NULL;
	}
}

const char *
mt_elf_pt_name(uint32_t type)
{
	switch (type) {
	case MT_PT_NULL: return "NULL";
	case MT_PT_LOAD: return "LOAD";
	case MT_PT_DYNAMIC: return "DYNAMIC";
	case MT_PT_INTERP: return "INTERP";
	case MT_PT_NOTE: return "NOTE";
	case MT_PT_SHLIB: return "SHLIB";
	case MT_PT_PHDR: return "PHDR";
	case MT_PT_TLS: return "TLS";
	case MT_PT_GNU_EH_FRAME: return "GNU_EH_FRAME";
	case MT_PT_GNU_STACK: return "GNU_STACK";
	case MT_PT_GNU_RELRO: return "GNU_RELRO";
	default: return NULL;
	}
}

/* 读取一个 program header (ELF64)。 */
enum mt_elf_status
mt_elf64_get_phdr(const void *bytes, size_t size,
                  const struct mt_elf64_view *view, uint16_t index,
                  struct mt_elf64_phdr *phdr)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;

	if (!p || !view || !phdr)
		return MT_ELF_E_ARGUMENT;
	if (index >= view->program_count)
		return MT_ELF_E_LAYOUT;
	if (view->program_entry_size < MT_ELF64_PHDR_SIZE)
		return MT_ELF_E_LAYOUT;
	if ((uint64_t)index > UINT64_MAX / view->program_entry_size)
		return MT_ELF_E_LAYOUT;
	offset = view->program_offset + (uint64_t)index * view->program_entry_size;
	if (offset < view->program_offset ||
	    !range_valid(offset, MT_ELF64_PHDR_SIZE, size))
		return MT_ELF_E_LAYOUT;

	p += offset;
	memset(phdr, 0, sizeof(*phdr));
	phdr->type = read32(p + 0);
	phdr->flags = read32(p + 4);
	phdr->offset = read64(p + 8);
	phdr->vaddr = read64(p + 16);
	phdr->paddr = read64(p + 24);
	phdr->filesz = read64(p + 32);
	phdr->memsz = read64(p + 40);
	phdr->align = read64(p + 48);
	return MT_ELF_OK;
}

/* 读取一个重定位条目（RELA 或 REL）。
 * REL 节区没有 addend 字段，返回 addend=0。 */
enum mt_elf_status
mt_elf64_get_rela(const void *bytes, size_t size,
                  const struct mt_elf64_section *table, uint64_t index,
                  struct mt_elf64_rela *rela)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;
	uint64_t entry_size;
	int is_rela;

	if (!p || !table || !rela)
		return MT_ELF_E_ARGUMENT;
	if (table->type != MT_SHT_RELA && table->type != MT_SHT_REL)
		return MT_ELF_E_ARGUMENT;
	is_rela = (table->type == MT_SHT_RELA);
	entry_size = is_rela ? 24 : 16;  /* Elf64_Rela = 24, Elf64_Rel = 16 */
	if (table->entry_size != 0 && table->entry_size < entry_size)
		return MT_ELF_E_LAYOUT;
	if (table->size % entry_size != 0 || index >= table->size / entry_size)
		return MT_ELF_E_LAYOUT;
	if (index > UINT64_MAX / entry_size)
		return MT_ELF_E_LAYOUT;
	offset = table->offset + index * entry_size;
	if (offset < table->offset || !range_valid(offset, entry_size, size))
		return MT_ELF_E_LAYOUT;

	p += offset;
	memset(rela, 0, sizeof(*rela));
	rela->offset = read64(p + 0);
	rela->info = read64(p + 8);
	rela->addend = is_rela ? (int64_t)read64(p + 16) : 0;
	return MT_ELF_OK;
}

/* 按名称查找节区。使用 section_name_index 指向的 shstrtab。 */
enum mt_elf_status
mt_elf64_find_section(const void *bytes, size_t size,
                      const struct mt_elf64_view *view, const char *name,
                      struct mt_elf64_section *section)
{
	enum mt_elf_status st;
	struct mt_elf64_section shstrtab;
	uint16_t i;

	if (!bytes || !view || !name || !section)
		return MT_ELF_E_ARGUMENT;
	if (view->section_name_index >= view->section_count)
		return MT_ELF_E_LAYOUT;
	st = mt_elf64_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK)
		return st;
	if (shstrtab.type != MT_SHT_STRTAB)
		return MT_ELF_E_LAYOUT;
	for (i = 0; i < view->section_count; ++i) {
		struct mt_elf64_section cur;
		const char *sname;
		st = mt_elf64_get_section(bytes, size, view, i, &cur);
		if (st != MT_ELF_OK)
			return st;
		st = mt_elf64_get_string(bytes, size, &shstrtab, cur.name, &sname);
		if (st != MT_ELF_OK)
			return st;
		if (strcmp(sname, name) == 0) {
			*section = cur;
			return MT_ELF_OK;
		}
	}
	return MT_ELF_E_LAYOUT;  /* not found */
}
