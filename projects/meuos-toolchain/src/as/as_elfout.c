/* as_elfout.c - mt/as ELF ET_REL output writing.
 *
 * Assembles the output object file: string/reloc tables, symbol table,
 * section/symbol/reloc data, ELF header and section headers, and the
 * final write_object() that streams it out.  Extracted from assemble.c.
 */
#include "mt/as.h"
#include "mt/as_int.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int
string_init(struct string_table *table)
{
	memset(table, 0, sizeof(*table));
	table->data = (unsigned char *)mt_malloc(1);
	if (!table->data)
		return -1;
	table->data[0] = 0;
	table->size = 1;
	table->capacity = 1;
	return 0;
}

static int
string_add(struct string_table *table, const char *text, uint32_t *offset)
{
	size_t length = strlen(text);
	size_t needed;
	unsigned char *data;
	size_t capacity;

	if (length > UINT32_MAX || table->size > SIZE_MAX - length - 1)
		return -1;
	needed = table->size + length + 1;
	if (needed > table->capacity) {
		capacity = table->capacity ? table->capacity * 2 : 64;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2) {
				capacity = needed;
				break;
			}
			capacity *= 2;
		}
		data = (unsigned char *)mt_realloc(table->data, capacity);
		if (!data)
			return -1;
		table->data = data;
		table->capacity = capacity;
	}
	*offset = (uint32_t)table->size;
	memcpy(table->data + table->size, text, length + 1);
	table->size = needed;
	return 0;
}

static int
reloc_add(struct reloc_group *group, uint64_t offset, unsigned type,
          uint32_t symbol, int64_t addend)
{
	struct out_reloc *items;
	if (group->count == group->capacity) {
		size_t capacity = group->capacity ? group->capacity * 2 : 16;
		items = (struct out_reloc *)mt_realloc(group->items,
		                                       capacity * sizeof(*items));
		if (!items)
			return -1;
		group->items = items;
		group->capacity = capacity;
	}
	group->items[group->count++] = (struct out_reloc){
		.offset = offset, .type = type, .symbol = symbol, .addend = addend
	};
	return 0;
}

static int
patch_le(struct as_file *as, struct as_section *section, size_t offset,
         unsigned width, uint64_t value)
{
	unsigned i;
	if (offset > section->size || width > section->size - offset)
		return as_error(as, "internal relocation offset out of range");
	if (width != 4 && width != 8 && width != 2 && width != 1)
		return as_error(as, "unsupported relocation width");
	for (i = 0; i < width; ++i)
		section->data[offset + i] = (unsigned char)(value >> (i * 8));
	return 0;
}

static int
build_output_sections(struct as_file *as, struct out_section **out_sections,
                      size_t *out_count, int **section_map,
                      struct reloc_group *groups, int elf_class)
{
	struct out_section *out;
	int *map;
	size_t count = 1;
	size_t i;
	int include;

	for (i = 0; i < as->section_count; ++i)
		++count;
	for (i = 0; i < as->section_count; ++i)
		if (groups[i].count != 0)
			++count;
	count += 3; /* symtab, strtab, shstrtab */
	out = (struct out_section *)calloc(count, sizeof(*out));
	map = (int *)mt_malloc(as->section_count * sizeof(*map));
	if (!out || (!map && as->section_count != 0)) {
		free(out);
		free(map);
		return -1;
	}
	for (i = 0; i < as->section_count; ++i) {
		map[i] = (int)(i + 1);
		out[i + 1].name = as->sections[i].name;
		out[i + 1].type = as->sections[i].type;
		out[i + 1].flags = as->sections[i].flags;
		out[i + 1].align = as->sections[i].align;
		out[i + 1].data = as->sections[i].data;
		out[i + 1].size = as->sections[i].size;
		out[i + 1].nobits = as->sections[i].type == MT_SHT_NOBITS;
	}
	include = (int)(as->section_count + 1);
	for (i = 0; i < as->section_count; ++i) {
		if (groups[i].count == 0)
			continue;
		out[include].name = NULL; /* assigned after section name is built */
		out[include].type = MT_SHT_RELA;
		out[include].flags = 0;
		out[include].align = 8;
		out[include].entry_size = (elf_class == 1) ? 12 : 24;
		out[include].info = (uint32_t)map[i];
		++include;
	}
	*out_count = count;
	*out_sections = out;
	*section_map = map;
	return 0;
}

static char *
reloc_section_name(const char *name)
{
	const char prefix[] = ".rela";
	size_t n = sizeof(prefix) - 1 + strlen(name) + 1;
	char *result = (char *)mt_malloc(n);
	if (!result)
		return NULL;
	strcpy(result, prefix);
	strcat(result, name);
	return result;
}

static int
build_symbols(struct as_file *as, const int *section_map, struct string_table *strtab,
              struct elf_sym_out **symbols_out, size_t *symbol_count,
              size_t *local_count)
{
	struct elf_sym_out *symbols;
	size_t count = 1;
	size_t locals = 1;
	size_t i;
	uint16_t shndx;
	uint32_t name_offset;

	if (string_init(strtab) != 0)
		return -1;
	/* Null symbol plus one local section symbol per input section. */
	count += as->section_count;
	locals += as->section_count;
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind == MT_STB_LOCAL)
			++locals;
		++count;
	}
	symbols = (struct elf_sym_out *)calloc(count, sizeof(*symbols));
	if (!symbols)
		return -1;
	count = 1;
	for (i = 0; i < as->section_count; ++i) {
		symbols[count++] = (struct elf_sym_out){
			.info = MT_ST_INFO(MT_STB_LOCAL, MT_STT_SECTION),
			.section = (uint16_t)section_map[i]
		};
	}
	/* Reorder source symbols so all locals precede globals as ELF requires. */
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind != MT_STB_LOCAL)
			continue;
		if (string_add(strtab, as->symbols[i].name, &name_offset) != 0)
			goto fail;
		shndx = as->symbols[i].defined && as->symbols[i].section >= 0 ?
		        (uint16_t)section_map[as->symbols[i].section] :
		        as->symbols[i].section == -2 ? MT_SHN_COMMON :
		        as->symbols[i].defined && as->symbols[i].section == -1 ? MT_SHN_ABS : 0;
		symbols[count] = (struct elf_sym_out){
			.name = name_offset,
			.info = MT_ST_INFO(as->symbols[i].bind, as->symbols[i].type),
			.other = (uint8_t)as->symbols[i].other,
			.section = shndx,
			.value = as->symbols[i].value,
			.size = as->symbols[i].size
		};
		as->symbols[i].output_index = (uint32_t)count++;
	}
	*local_count = count;
	for (i = 0; i < as->symbol_count; ++i) {
		if (as->symbols[i].bind == MT_STB_LOCAL)
			continue;
		if (string_add(strtab, as->symbols[i].name, &name_offset) != 0)
			goto fail;
		shndx = as->symbols[i].defined && as->symbols[i].section >= 0 ?
		        (uint16_t)section_map[as->symbols[i].section] :
		        as->symbols[i].section == -2 ? MT_SHN_COMMON :
		        as->symbols[i].defined && as->symbols[i].section == -1 ? MT_SHN_ABS : 0;
		symbols[count] = (struct elf_sym_out){
			.name = name_offset,
			.info = MT_ST_INFO(as->symbols[i].bind, as->symbols[i].type),
			.other = (uint8_t)as->symbols[i].other,
			.section = shndx,
			.value = as->symbols[i].value,
			.size = as->symbols[i].size
		};
		as->symbols[i].output_index = (uint32_t)count++;
	}
	*symbols_out = symbols;
	*symbol_count = count;
	*local_count = locals;
	return 0;
fail:
	free(symbols);
	return -1;
}

static int
resolve_fixups(struct as_file *as, const int *section_map,
               struct reloc_group *groups)
{
	size_t i;
	struct as_fixup *fix;
	struct as_symbol *symbol;
	struct as_section *section;
	int can_resolve;
	int64_t value;

	(void)section_map;
	for (i = 0; i < as->fixup_count; ++i) {
		fix = &as->fixups[i];
		section = &as->sections[fix->section];
		symbol = find_symbol(as, fix->symbol);
		if (!symbol)
			return as_error(as, "undefined internal symbol: %s", fix->symbol);
		/* Only PC-relative relocations can be folded at assembly time
		 * (the same-section offset delta is known).  Absolute data
		 * relocations must always go to the linker: the section base
		 * address is only known after layout, so folding `.quad sym`
		 * to the section offset would yield 0 instead of the runtime
		 * address of sym.  The numeric constants collide across
		 * targets, so key off the machine. */
		{
			int is_x86 = as->target &&
			             (as->target->emachine == MT_EM_X86_64 ||
			              as->target->emachine == MT_EM_386);
			int is_arm = as->target &&
			             as->target->emachine == MT_EM_ARM;
			int raw_resolvable =
			    (is_x86 && (fix->type == 2 || fix->type == 4)) ||
			    (is_arm && (fix->type == 3));
			can_resolve = symbol->defined &&
			              symbol->section == fix->section &&
			              raw_resolvable;
		}
		if (can_resolve) {
			value = (int64_t)symbol->value + fix->addend -
			        (fix->type == MT_R_X86_64_PC32 ||
			         fix->type == MT_R_X86_64_PLT32 ?
			         (int64_t)fix->offset : 0);
			if (patch_le(as, section, fix->offset, fix->width,
			             (uint64_t)value) != 0)
				return -1;
			continue;
		}
		if (reloc_add(&groups[fix->section], fix->offset, fix->type,
		              symbol->output_index, fix->addend) != 0)
			return as_error(as, "out of memory");
	}
	return 0;
}

static int
write_u16(FILE *file, uint16_t value)
{
	unsigned char p[2] = {(unsigned char)value, (unsigned char)(value >> 8)};
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_u32(FILE *file, uint32_t value)
{
	unsigned char p[4] = {(unsigned char)value, (unsigned char)(value >> 8),
	                      (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_u64(FILE *file, uint64_t value)
{
	unsigned char p[8];
	unsigned i;
	for (i = 0; i < 8; ++i)
		p[i] = (unsigned char)(value >> (i * 8));
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_zeros(FILE *file, uint64_t count)
{
	static const unsigned char zero[256];
	while (count != 0) {
		size_t n = count < sizeof(zero) ? (size_t)count : sizeof(zero);
		if (fwrite(zero, 1, n, file) != n)
			return -1;
		count -= n;
	}
	return 0;
}

static uint64_t
align_up_u64(uint64_t value, uint64_t align)
{
	uint64_t mask = align - 1;
	return (value + mask) & ~mask;
}

static int
write_elf_header(FILE *file, const struct mt_target *target,
                 uint64_t section_offset, uint16_t section_count,
                 uint16_t section_string_index)
{
	unsigned char ident[16] = {0x7f, 'E', 'L', 'F',
	                           target->elf_class, target->elf_endian, 1, 0};
	if (target->elf_class == 1) {
		/* ELF32 header (52 bytes) */
		if (fseek(file, 0, SEEK_SET) != 0 ||
		    fwrite(ident, 1, sizeof(ident), file) != sizeof(ident) ||
		    write_u16(file, 1) != 0 ||
		    write_u16(file, target->emachine) != 0 ||
		    write_u32(file, 1) != 0 ||
		    write_u32(file, 0) != 0 ||          /* entry */
		    write_u32(file, 0) != 0 ||          /* phoff */
		    write_u32(file, (uint32_t)section_offset) != 0 ||
		    write_u32(file, target->e_flags) != 0 ||
		    write_u16(file, target->ehdr_size) != 0 ||
		    write_u16(file, 0) != 0 ||          /* phentsize */
		    write_u16(file, 0) != 0 ||          /* phnum */
		    write_u16(file, target->shdr_size) != 0 ||
		    write_u16(file, section_count) != 0 ||
		    write_u16(file, section_string_index) != 0)
			return -1;
	} else {
		/* ELF64 header (64 bytes) */
		if (fseek(file, 0, SEEK_SET) != 0 ||
		    fwrite(ident, 1, sizeof(ident), file) != sizeof(ident) ||
		    write_u16(file, 1) != 0 ||
		    write_u16(file, target->emachine) != 0 ||
		    write_u32(file, 1) != 0 ||
		    write_u64(file, 0) != 0 ||
		    write_u64(file, 0) != 0 ||
		    write_u64(file, section_offset) != 0 ||
		    write_u32(file, target->e_flags) != 0 ||
		    write_u16(file, target->ehdr_size) != 0 ||
		    write_u16(file, 0) != 0 ||
		    write_u16(file, 0) != 0 ||
		    write_u16(file, target->shdr_size) != 0 ||
		    write_u16(file, section_count) != 0 ||
		    write_u16(file, section_string_index) != 0)
			return -1;
	}
	return 0;
}

static int
write_section_header(FILE *file, const struct out_section *section,
                     uint32_t name_offset, int elf_class)
{
	if (write_u32(file, name_offset) != 0 || write_u32(file, section->type) != 0)
		return -1;
	if (elf_class == 1) {
		/* ELF32: 40-byte section header */
		if (write_u32(file, (uint32_t)section->flags) != 0 ||
		    write_u32(file, 0) != 0 ||  /* addr */
		    write_u32(file, (uint32_t)section->file_offset) != 0 ||
		    write_u32(file, (uint32_t)section->size) != 0 ||
		    write_u32(file, section->link) != 0 ||
		    write_u32(file, section->info) != 0 ||
		    write_u32(file, section->align ? (uint32_t)section->align : 1) != 0 ||
		    write_u32(file, (uint32_t)section->entry_size) != 0)
			return -1;
	} else {
		/* ELF64: 64-byte section header */
		if (write_u64(file, section->flags) != 0 || write_u64(file, 0) != 0 ||
		    write_u64(file, section->file_offset) != 0 || write_u64(file, section->size) != 0 ||
		    write_u32(file, section->link) != 0 || write_u32(file, section->info) != 0 ||
		    write_u64(file, section->align ? section->align : 1) != 0 ||
		    write_u64(file, section->entry_size) != 0)
			return -1;
	}
	return 0;
}

static void
mem_u32(unsigned char **cursor, uint32_t value)
{
	(*cursor)[0] = (unsigned char)value;
	(*cursor)[1] = (unsigned char)(value >> 8);
	(*cursor)[2] = (unsigned char)(value >> 16);
	(*cursor)[3] = (unsigned char)(value >> 24);
	*cursor += 4;
}

static void
mem_u64(unsigned char **cursor, uint64_t value)
{
	unsigned i;
	for (i = 0; i < 8; ++i)
		(*cursor)[i] = (unsigned char)(value >> (i * 8));
	*cursor += 8;
}

static void
build_symbol_data(unsigned char *data, const struct elf_sym_out *symbols,
                  size_t count, int elf_class)
{
	unsigned char *cursor = data;
	size_t i;
	for (i = 0; i < count; ++i) {
		mem_u32(&cursor, symbols[i].name);
		if (elf_class == 1) {
			/* ELF32 symbol (16 bytes): name, value, size, info, other, shndx */
			mem_u32(&cursor, (uint32_t)symbols[i].value);
			mem_u32(&cursor, (uint32_t)symbols[i].size);
			*cursor++ = symbols[i].info;
			*cursor++ = symbols[i].other;
			*cursor++ = (unsigned char)symbols[i].section;
			*cursor++ = (unsigned char)(symbols[i].section >> 8);
		} else {
			/* ELF64 symbol (24 bytes): name, info, other, shndx, value, size */
			*cursor++ = symbols[i].info;
			*cursor++ = symbols[i].other;
			*cursor++ = (unsigned char)symbols[i].section;
			*cursor++ = (unsigned char)(symbols[i].section >> 8);
			mem_u64(&cursor, symbols[i].value);
			mem_u64(&cursor, symbols[i].size);
		}
	}
}

static void
build_reloc_data(unsigned char *data, const struct reloc_group *group,
                 int elf_class)
{
	unsigned char *cursor = data;
	size_t i;
	uint64_t info;
	for (i = 0; i < group->count; ++i) {
		info = ((uint64_t)group->items[i].symbol << 32) |
		       group->items[i].type;
		if (elf_class == 1) {
			/* ELF32 RELA entry (12 bytes): offset, info, addend.
			 * ELF32 r_info packs the symbol index in bits [31:8]
			 * and the type in the low 8 bits. */
			mem_u32(&cursor, (uint32_t)group->items[i].offset);
			mem_u32(&cursor,
			        ((uint32_t)group->items[i].symbol << 8) |
			        group->items[i].type);
			mem_u32(&cursor, (uint32_t)group->items[i].addend);
		} else {
			/* ELF64 RELA entry (24 bytes): offset, info, addend */
			mem_u64(&cursor, group->items[i].offset);
			mem_u64(&cursor, info);
			mem_u64(&cursor, (uint64_t)group->items[i].addend);
		}
	}
}

static void
free_reloc_groups(struct reloc_group *groups, size_t count)
{
	size_t i;
	for (i = 0; i < count; ++i)
		free(groups[i].items);
	free(groups);
}

int
write_object(struct as_file *as, const struct mt_target *target,
             const char *output_path)
{
	struct reloc_group *groups = NULL;
	struct out_section *out = NULL;
	struct elf_sym_out *symbols = NULL;
	struct string_table strtab;
	struct string_table shstrtab;
	int *section_map = NULL;
	uint32_t *section_name_offsets = NULL;
	uint32_t *reloc_output_map = NULL;
	unsigned char *symtab_data = NULL;
	FILE *file = NULL;
	size_t out_count = 0;
	size_t symbol_count = 0;
	size_t local_count = 0;
	size_t symtab_index;
	size_t strtab_index;
	size_t shstrtab_index;
	size_t i, pos;
	uint32_t shstr_string_index;
	uint64_t offset;
	uint64_t section_offset;
	uint64_t section_size;
	uint64_t align;
	int result = -1;

	memset(&strtab, 0, sizeof(strtab));
	memset(&shstrtab, 0, sizeof(shstrtab));
	if (as->section_count > SIZE_MAX / sizeof(*groups))
		return as_error(as, "too many sections");
	groups = (struct reloc_group *)calloc(as->section_count, sizeof(*groups));
	section_map = (int *)mt_malloc(as->section_count * sizeof(*section_map));
	if (!groups || (!section_map && as->section_count != 0)) {
		as_error(as, "out of memory");
		goto out;
	}
	for (i = 0; i < as->section_count; ++i)
		section_map[i] = (int)i + 1;
	if (build_symbols(as, section_map, &strtab, &symbols,
	                  &symbol_count, &local_count) != 0) {
		as_error(as, "unable to build ELF symbol table");
		goto out;
	}
	if (resolve_fixups(as, section_map, groups) != 0)
		goto out;
	if (build_output_sections(as, &out, &out_count, &section_map, groups,
	                          target->elf_class) != 0) {
		as_error(as, "unable to build ELF section table");
		goto out;
	}
	if (out_count > UINT16_MAX) {
		as_error(as, "too many output sections");
		goto out;
	}
	free(reloc_output_map);
	reloc_output_map = (uint32_t *)calloc(as->section_count,
	                                      sizeof(*reloc_output_map));
	if (!reloc_output_map && as->section_count != 0) {
		as_error(as, "out of memory");
		goto out;
	}
	pos = as->section_count + 1;
	for (i = 0; i < as->section_count; ++i) {
		char *reloc_name;
		if (groups[i].count == 0)
			continue;
		reloc_output_map[i] = (uint32_t)pos;
		reloc_name = reloc_section_name(as->sections[i].name);
		if (!reloc_name) {
			as_error(as, "out of memory");
			goto out;
		}
		out[pos].name = reloc_name;
		++pos;
	}
	symtab_index = out_count - 3;
	strtab_index = out_count - 2;
	shstrtab_index = out_count - 1;
	for (i = as->section_count + 1; i < symtab_index; ++i)
		out[i].link = (uint32_t)symtab_index;
	out[symtab_index].name = ".symtab";
	out[symtab_index].type = MT_SHT_SYMTAB;
	out[symtab_index].flags = 0;
	out[symtab_index].align = 8;
	out[symtab_index].entry_size = (target->elf_class == 1) ? 16 : 24;
	out[symtab_index].link = (uint32_t)strtab_index;
	out[symtab_index].info = (uint32_t)local_count;
	out[strtab_index].name = ".strtab";
	out[strtab_index].type = MT_SHT_STRTAB;
	out[strtab_index].align = 1;
	out[shstrtab_index].name = ".shstrtab";
	out[shstrtab_index].type = MT_SHT_STRTAB;
	out[shstrtab_index].align = 1;
	if (string_init(&shstrtab) != 0)
		goto out;
	section_name_offsets = (uint32_t *)calloc(out_count, sizeof(*section_name_offsets));
	if (!section_name_offsets) {
		as_error(as, "out of memory");
		goto out;
	}
	for (i = 1; i < out_count; ++i) {
		if (!out[i].name || string_add(&shstrtab, out[i].name,
		                               &section_name_offsets[i]) != 0) {
			as_error(as, "unable to build section name table");
			goto out;
		}
	}
	if (string_add(&shstrtab, ".shstrtab", &shstr_string_index) != 0)
		goto out;
	(void)shstr_string_index;
	out[symtab_index].size = symbol_count * ((target->elf_class == 1) ? 16 : 24);
	out[symtab_index].data = (unsigned char *)mt_malloc(out[symtab_index].size);
	if (!out[symtab_index].data)
		goto out;
	symtab_data = out[symtab_index].data;
	build_symbol_data(symtab_data, symbols, symbol_count, target->elf_class);
	out[strtab_index].data = strtab.data;
	out[strtab_index].size = strtab.size;
	strtab.data = NULL;
	out[shstrtab_index].data = shstrtab.data;
	out[shstrtab_index].size = shstrtab.size;
	shstrtab.data = NULL;
	for (i = 0; i < as->section_count; ++i) {
		if (groups[i].count == 0)
			continue;
		section_size = groups[i].count *
		               ((target->elf_class == 1) ? 12 : 24);
		out[reloc_output_map[i]].data =
		    (unsigned char *)mt_malloc((size_t)section_size);
		if (!out[reloc_output_map[i]].data)
			goto out;
		out[reloc_output_map[i]].size = section_size;
		build_reloc_data(out[reloc_output_map[i]].data, &groups[i],
		                 target->elf_class);
	}
	/* Assign file offsets. NOBITS reserves virtual size but no file bytes. */
	offset = target->ehdr_size;
	for (i = 1; i < out_count; ++i) {
		align = out[i].align ? out[i].align : 1;
		offset = align_up_u64(offset, align);
		out[i].file_offset = offset;
		if (!out[i].nobits) {
			if (out[i].size > UINT64_MAX - offset)
				goto out;
			offset += out[i].size;
		}
	}
	section_offset = align_up_u64(offset, 8);
	file = fopen(output_path, "wb+");
	if (!file) {
		as_error(as, "cannot open output %s: %s", output_path, strerror(errno));
		goto out;
	}
	if (write_elf_header(file, target, section_offset, (uint16_t)out_count,
	                     (uint16_t)shstrtab_index) != 0)
		goto out;
	for (i = 1; i < out_count; ++i) {
		if (out[i].nobits || out[i].size == 0)
			continue;
		if (fseek(file, (long)out[i].file_offset, SEEK_SET) != 0 ||
		    fwrite(out[i].data, 1, (size_t)out[i].size, file) != out[i].size)
			goto out;
	}
	if (fseek(file, (long)section_offset, SEEK_SET) != 0 ||
	    write_zeros(file, target->shdr_size) != 0)
		goto out;
	for (i = 1; i < out_count; ++i)
		if (write_section_header(file, &out[i], section_name_offsets[i], target->elf_class) != 0)
			goto out;
	if (fclose(file) != 0) {
		file = NULL;
		goto out;
	}
	file = NULL;
	result = 0;
out:
	if (file)
		fclose(file);
	free(symtab_data);
	free(symbols);
	free(section_name_offsets);
	free(reloc_output_map);
	free(section_map);
	if (out) {
		for (i = as->section_count + 1; i < out_count - 3; ++i)
			free((char *)out[i].name);
		free(out[out_count - 2].data);
		free(out[out_count - 1].data);
		free(out);
	}
	free(strtab.data);
	free(shstrtab.data);
	free_reloc_groups(groups, as->section_count);
	return result;
}
