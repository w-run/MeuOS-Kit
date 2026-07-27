/* elf32.c – MeuOS Toolchain ELF32 little-endian reader. */
#include "mt/elf.h"
#include "mt/elf32.h"

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

static int
range_valid(uint32_t offset, uint32_t length, size_t size)
{
	if (offset > (uint32_t)size)
		return 0;
	return length <= (uint32_t)size - offset;
}

static int
table_valid(uint32_t offset, uint16_t entry_size,
            uint16_t count, size_t size)
{
	uint32_t length;
	if (count == 0)
		return 1;
	if (entry_size == 0)
		return 0;
	if ((uint32_t)count > UINT32_MAX / (uint32_t)entry_size)
		return 0;
	length = (uint32_t)entry_size * (uint32_t)count;
	return range_valid(offset, length, size);
}

enum mt_elf_status
mt_elf32_parse(const void *bytes, size_t size, struct mt_elf32_view *view)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint16_t phentsize, phnum, shentsize, shnum;

	if (!p || !view)
		return MT_ELF_E_ARGUMENT;
	if (size < MT_ELF32_EHDR_SIZE)
		return MT_ELF_E_TRUNCATED;
	if (memcmp(p, "\177ELF", 4) != 0)
		return MT_ELF_E_MAGIC;
	if (p[4] != 1)  /* ELFCLASS32 */
		return MT_ELF_E_CLASS;
	if (p[5] != MT_ELFDATA2LSB)
		return MT_ELF_E_ENCODING;
	if (p[6] != MT_EV_CURRENT || read32(p + 20) != MT_EV_CURRENT)
		return MT_ELF_E_VERSION;

	phentsize = read16(p + 42);
	phnum = read16(p + 44);
	shentsize = read16(p + 46);
	shnum = read16(p + 48);
	if (read16(p + 40) < MT_ELF32_EHDR_SIZE ||
	    (phnum && phentsize < MT_ELF32_PHDR_SIZE) ||
	    (shnum && shentsize < MT_ELF32_SHDR_SIZE) ||
	    !table_valid(read32(p + 28), phentsize, phnum, size) ||
	    !table_valid(read32(p + 36), shentsize, shnum, size))
		return MT_ELF_E_LAYOUT;

	memset(view, 0, sizeof(*view));
	view->type = read16(p + 16);
	view->machine = read16(p + 18);
	view->version = read32(p + 20);
	view->entry = read32(p + 24);
	view->program_offset = read32(p + 28);
	view->section_offset = read32(p + 32);
	view->flags = read32(p + 36);  /* not meaningful in practice */
	view->header_size = read16(p + 40);
	view->program_entry_size = phentsize;
	view->program_count = phnum;
	view->section_entry_size = shentsize;
	view->section_count = shnum;
	view->section_name_index = read16(p + 50);
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf32_get_section(const void *bytes, size_t size,
                     const struct mt_elf32_view *view, uint16_t index,
                     struct mt_elf32_section *section)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;

	if (!p || !view || !section)
		return MT_ELF_E_ARGUMENT;
	if (index >= view->section_count)
		return MT_ELF_E_LAYOUT;
	if (view->section_entry_size < MT_ELF32_SHDR_SIZE)
		return MT_ELF_E_LAYOUT;
	if ((uint64_t)index > UINT64_MAX / view->section_entry_size)
		return MT_ELF_E_LAYOUT;
	offset = view->section_offset +
	         (uint64_t)index * view->section_entry_size;
	if (offset < view->section_offset ||
	    (uint64_t)offset + MT_ELF32_SHDR_SIZE > size)
		return MT_ELF_E_LAYOUT;

	p += (uint64_t)offset;
	memset(section, 0, sizeof(*section));
	section->name = read32(p + 0);
	section->type = read32(p + 4);
	section->flags = read32(p + 8);
	section->address = read32(p + 12);
	section->offset = read32(p + 16);
	section->size = read32(p + 20);
	section->link = read32(p + 24);
	section->info = read32(p + 28);
	section->alignment = read32(p + 32);
	section->entry_size = read32(p + 36);
	if (section->type != MT_SHT_NOBITS &&
	    !range_valid(section->offset, section->size, size))
		return MT_ELF_E_LAYOUT;
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf32_get_symbol(const void *bytes, size_t size,
                    const struct mt_elf32_section *table, uint64_t index,
                    struct mt_elf32_symbol *symbol)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;

	if (!p || !table || !symbol)
		return MT_ELF_E_ARGUMENT;
	if (table->type != MT_SHT_SYMTAB && table->type != MT_SHT_DYNSYM)
		return MT_ELF_E_ARGUMENT;
	if (table->entry_size < MT_ELF32_SYM_SIZE ||
	    table->size % table->entry_size != 0 ||
	    index >= table->size / table->entry_size)
		return MT_ELF_E_LAYOUT;
	if (index > UINT64_MAX / table->entry_size)
		return MT_ELF_E_LAYOUT;
	offset = table->offset + index * table->entry_size;
	if (offset < table->offset ||
	    (uint64_t)offset + MT_ELF32_SYM_SIZE > size)
		return MT_ELF_E_LAYOUT;

	p += (uint64_t)offset;
	memset(symbol, 0, sizeof(*symbol));
	symbol->name = read32(p + 0);
	symbol->info = p[4];
	symbol->other = p[5];
	symbol->section = read16(p + 6);
	symbol->value = read32(p + 8);
	symbol->size = read32(p + 12);
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf32_get_string(const void *bytes, size_t size,
                    const struct mt_elf32_section *strings, uint32_t offset,
                    const char **value)
{
	const unsigned char *p = (const unsigned char *)bytes;
	const unsigned char *end, *nul;

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

enum mt_elf_status
mt_elf32_get_phdr(const void *bytes, size_t size,
                  const struct mt_elf32_view *view, uint16_t index,
                  struct mt_elf32_phdr *phdr)
{
	const unsigned char *p = (const unsigned char *)bytes;
	uint64_t offset;

	if (!p || !view || !phdr)
		return MT_ELF_E_ARGUMENT;
	if (index >= view->program_count)
		return MT_ELF_E_LAYOUT;
	if (view->program_entry_size < MT_ELF32_PHDR_SIZE)
		return MT_ELF_E_LAYOUT;
	if ((uint64_t)index > UINT64_MAX / view->program_entry_size)
		return MT_ELF_E_LAYOUT;
	offset = view->program_offset + (uint64_t)index * view->program_entry_size;
	if (offset < view->program_offset ||
	    (uint64_t)offset + MT_ELF32_PHDR_SIZE > size)
		return MT_ELF_E_LAYOUT;

	p += (uint64_t)offset;
	memset(phdr, 0, sizeof(*phdr));
	phdr->type = read32(p + 0);
	phdr->offset = read32(p + 4);
	phdr->vaddr = read32(p + 8);
	phdr->paddr = read32(p + 12);
	phdr->filesz = read32(p + 16);
	phdr->memsz = read32(p + 20);
	phdr->flags = read32(p + 24);
	phdr->align = read32(p + 28);
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf32_get_rela(const void *bytes, size_t size,
                  const struct mt_elf32_section *table, uint64_t index,
                  struct mt_elf32_rela *rela)
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
	entry_size = is_rela ? 12 : 8;  /* Elf32_Rela=12, Elf32_Rel=8 */
	if (table->entry_size != 0 && table->entry_size < entry_size)
		return MT_ELF_E_LAYOUT;
	if (table->size % entry_size != 0 || index >= table->size / entry_size)
		return MT_ELF_E_LAYOUT;
	if (index > UINT64_MAX / entry_size)
		return MT_ELF_E_LAYOUT;
	offset = table->offset + index * entry_size;
	if (offset < table->offset ||
	    (uint64_t)offset + entry_size > size)
		return MT_ELF_E_LAYOUT;

	p += (uint64_t)offset;
	memset(rela, 0, sizeof(*rela));
	rela->offset = read32(p + 0);
	rela->info = read32(p + 4);
	rela->addend = is_rela ? (int32_t)read32(p + 8) : 0;
	return MT_ELF_OK;
}

enum mt_elf_status
mt_elf32_find_section(const void *bytes, size_t size,
                      const struct mt_elf32_view *view, const char *name,
                      struct mt_elf32_section *section)
{
	enum mt_elf_status st;
	struct mt_elf32_section shstrtab;
	uint16_t i;

	if (!bytes || !view || !name || !section)
		return MT_ELF_E_ARGUMENT;
	if (view->section_name_index >= view->section_count)
		return MT_ELF_E_LAYOUT;
	st = mt_elf32_get_section(bytes, size, view,
	                          view->section_name_index, &shstrtab);
	if (st != MT_ELF_OK)
		return st;
	if (shstrtab.type != MT_SHT_STRTAB)
		return MT_ELF_E_LAYOUT;
	for (i = 0; i < view->section_count; ++i) {
		struct mt_elf32_section cur;
		const char *sname;
		st = mt_elf32_get_section(bytes, size, view, i, &cur);
		if (st != MT_ELF_OK)
			return st;
		st = mt_elf32_get_string(bytes, size, &shstrtab, cur.name, &sname);
		if (st != MT_ELF_OK)
			return st;
		if (strcmp(sname, name) == 0) {
			*section = cur;
			return MT_ELF_OK;
		}
	}
	return MT_ELF_E_LAYOUT;
}
