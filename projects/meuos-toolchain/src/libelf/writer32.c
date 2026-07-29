/* writer32.c – MeuOS Toolchain ELF32 little-endian writer. */
#include "mt/elf.h"
#include "mt/elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

void
mt_elf32_writer_init(struct mt_elf32_writer *w, uint16_t machine,
                     uint16_t type)
{
	memset(w, 0, sizeof(*w));
	w->machine = machine;
	w->type = type;
}

void
mt_elf32_writer_free(struct mt_elf32_writer *w)
{
	free(w->sections);
	w->sections = NULL;
	w->section_count = 0;
	w->section_capacity = 0;
}

int
mt_elf32_writer_add_section(struct mt_elf32_writer *w,
                            const struct mt_elf32_writer_section *sec)
{
	struct mt_elf32_writer_section *p;
	if (w->section_count >= w->section_capacity) {
		size_t newcap = w->section_capacity ? w->section_capacity * 2 : 16;
		p = realloc(w->sections, newcap * sizeof(*p));
		if (!p) return -1;
		w->sections = p;
		w->section_capacity = newcap;
	}
	w->sections[w->section_count++] = *sec;
	return 0;
}

void
mt_elf32_writer_set_entry(struct mt_elf32_writer *w, uint32_t entry)
{
	w->entry = entry;
}

void
mt_elf32_writer_set_flags(struct mt_elf32_writer *w, uint32_t flags)
{
	w->flags = flags;
}

/* ---- internal helpers ---- */

static char *
build_shstrtab32(const struct mt_elf32_writer *w, uint32_t *name_offsets,
                 size_t *out_size)
{
	size_t total = 1;
	size_t i;
	char *buf;

	for (i = 0; i < w->section_count; ++i)
		total += strlen(w->sections[i].name) + 1;
	total += sizeof(".shstrtab");

	buf = malloc(total);
	if (!buf) return NULL;

	buf[0] = '\0';
	{
		size_t off = 1;
		for (i = 0; i < w->section_count; ++i) {
			size_t len = strlen(w->sections[i].name) + 1;
			name_offsets[i] = (uint32_t)off;
			memcpy(buf + off, w->sections[i].name, len);
			off += len;
		}
		name_offsets[w->section_count] = (uint32_t)off;
		memcpy(buf + off, ".shstrtab", sizeof(".shstrtab"));
	}
	*out_size = total;
	return buf;
}

static uint32_t
align_up32(uint32_t value, uint32_t align)
{
	if (align <= 1) return value;
	return (value + align - 1) & ~(align - 1);
}

int
mt_elf32_writer_finalize(struct mt_elf32_writer *w, FILE *out)
{
	size_t total_sections = w->section_count + 2;
	uint16_t shstrtab_index = (uint16_t)(w->section_count + 1);
	uint32_t *name_offsets;
	char *shstrtab;
	size_t shstrtab_size;
	uint32_t *data_offsets;
	uint32_t offset, shdr_offset;
	unsigned char ehdr[MT_ELF32_EHDR_SIZE];
	size_t i;

	name_offsets = calloc(total_sections, sizeof(uint32_t));
	if (!name_offsets)
		return -1;
	shstrtab = build_shstrtab32(w, name_offsets, &shstrtab_size);
	data_offsets = calloc(total_sections, sizeof(uint32_t));
	if (!shstrtab || !data_offsets) {
		free(name_offsets); free(shstrtab); free(data_offsets);
		return -1;
	}

	offset = MT_ELF32_EHDR_SIZE;
	data_offsets[0] = 0;
	for (i = 0; i < w->section_count; ++i) {
		struct mt_elf32_writer_section *s = &w->sections[i];
		offset = align_up32(offset, s->alignment);
		data_offsets[i + 1] = offset;
		if (s->type != MT_SHT_NOBITS)
			offset += s->size;
	}
	offset = align_up32(offset, 1);
	data_offsets[shstrtab_index] = offset;
	offset += (uint32_t)shstrtab_size;
	offset = align_up32(offset, 8);
	shdr_offset = offset;
	offset += (uint32_t)total_sections * MT_ELF32_SHDR_SIZE;

	/* ELF32 Ehdr layout: e_type(16,2) e_machine(18,2) e_version(20,4)
	          e_entry(24,4) e_phoff(28,4) e_shoff(32,4) e_flags(36,4)
	          e_ehsize(40,2) e_phentsize(42,2) e_phnum(44,2)
	          e_shentsize(46,2) e_shnum(48,2) e_shstrndx(50,2) */
	memset(ehdr, 0, sizeof(ehdr));
	ehdr[0] = 0x7f; ehdr[1] = 'E'; ehdr[2] = 'L'; ehdr[3] = 'F';
	ehdr[4] = 1;              /* ELFCLASS32 */
	ehdr[5] = MT_ELFDATA2LSB;
	ehdr[6] = MT_EV_CURRENT;
	ehdr[7] = 0;              /* OSABI_NONE */
	put16(ehdr + 16, w->type);
	put16(ehdr + 18, w->machine);
	put32(ehdr + 20, MT_EV_CURRENT);
	put32(ehdr + 24, w->entry);
	put32(ehdr + 28, 0);      /* e_phoff */
	put32(ehdr + 32, shdr_offset);
	put32(ehdr + 36, w->flags);
	put16(ehdr + 40, MT_ELF32_EHDR_SIZE);
	put16(ehdr + 42, 0);      /* phentsize */
	put16(ehdr + 44, 0);      /* phnum */
	put16(ehdr + 46, MT_ELF32_SHDR_SIZE);
	put16(ehdr + 48, (uint16_t)total_sections);
	put16(ehdr + 50, shstrtab_index);

	if (fwrite(ehdr, 1, sizeof(ehdr), out) != sizeof(ehdr))
		goto io_err;

	for (i = 0; i < w->section_count; ++i) {
		struct mt_elf32_writer_section *s = &w->sections[i];
		if (s->type == MT_SHT_NOBITS || s->size == 0)
			continue;
		if (fseek(out, (long)data_offsets[i + 1], SEEK_SET) != 0)
			goto io_err;
		if (fwrite(s->data, 1, s->size, out) != s->size)
			goto io_err;
	}

	if (fseek(out, (long)data_offsets[shstrtab_index], SEEK_SET) != 0)
		goto io_err;
	if (fwrite(shstrtab, 1, shstrtab_size, out) != shstrtab_size)
		goto io_err;

	if (fseek(out, (long)shdr_offset, SEEK_SET) != 0)
		goto io_err;
	for (i = 0; i < total_sections; ++i) {
		unsigned char shdr[MT_ELF32_SHDR_SIZE];
		memset(shdr, 0, sizeof(shdr));
		if (i == 0) {
			/* NULL */
		} else if (i == shstrtab_index) {
			put32(shdr + 0, name_offsets[w->section_count]);
			put32(shdr + 4, MT_SHT_STRTAB);
			put32(shdr + 16, data_offsets[shstrtab_index]);
			put32(shdr + 20, (uint32_t)shstrtab_size);
			put32(shdr + 32, 1);
		} else {
			struct mt_elf32_writer_section *s = &w->sections[i - 1];
			put32(shdr + 0, name_offsets[i - 1]);
			put32(shdr + 4, s->type);
			put32(shdr + 8, s->flags);
			put32(shdr + 12, s->address);
			put32(shdr + 16, data_offsets[i]);
			put32(shdr + 20, s->size);
			put32(shdr + 24, s->link);
			put32(shdr + 28, s->info);
			put32(shdr + 32, s->alignment);
			put32(shdr + 36, s->entry_size);
		}
		if (fwrite(shdr, 1, sizeof(shdr), out) != sizeof(shdr))
			goto io_err;
	}

	free(shstrtab); free(name_offsets); free(data_offsets);
	return 0;

io_err:
	free(shstrtab); free(name_offsets); free(data_offsets);
	return -1;
}
