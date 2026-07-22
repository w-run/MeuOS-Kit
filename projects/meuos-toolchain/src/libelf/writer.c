/* writer.c - MeuOS Toolchain ELF64 little-endian writer.
 *
 * 构造一个完整的 ELF64 文件：ehdr + 节区数据 + shdr 表 + shstrtab。
 * 调用方按顺序追加节区，writer 在 finalize 时自动生成 NULL 节区
 * (index 0) 和 .shstrtab 节区 (最后一个)。输出可复现（无时间戳）。 */
#include "mt/elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little-endian 编码 ---- */

static void
put16(unsigned char *p, uint16_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void
put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void
put64(unsigned char *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

/* ---- 公共 API ---- */

void
mt_elf64_writer_init(struct mt_elf64_writer *w, uint16_t machine,
                     uint16_t type)
{
	memset(w, 0, sizeof(*w));
	w->machine = machine;
	w->type = type;
}

void
mt_elf64_writer_free(struct mt_elf64_writer *w)
{
	free(w->sections);
	w->sections = NULL;
	w->section_count = 0;
	w->section_capacity = 0;
}

int
mt_elf64_writer_add_section(struct mt_elf64_writer *w,
                            const struct mt_elf64_writer_section *sec)
{
	struct mt_elf64_writer_section *p;

	if (w->section_count >= w->section_capacity) {
		size_t newcap = w->section_capacity ? w->section_capacity * 2 : 16;
		p = realloc(w->sections, newcap * sizeof(*p));
		if (!p)
			return -1;
		w->sections = p;
		w->section_capacity = newcap;
	}
	w->sections[w->section_count++] = *sec;
	return 0;
}

void
mt_elf64_writer_set_entry(struct mt_elf64_writer *w, uint64_t entry)
{
	w->entry = entry;
}

void
mt_elf64_writer_set_flags(struct mt_elf64_writer *w, uint32_t flags)
{
	w->flags = flags;
}

/* ---- finalize 实现 ---- */

/* 构建 .shstrtab 内容。返回 malloc'd buffer。
 * name_offsets 数组大小为 section_count + 1：
 *   name_offsets[i] = sections[i] 的名字在 shstrtab 中的偏移
 *   name_offsets[section_count] = ".shstrtab" 的偏移
 * 节区 0 (NULL) 的名字偏移始终为 0（空字符串）。 */
static char *
build_shstrtab(const struct mt_elf64_writer *w, uint32_t *name_offsets,
               size_t *out_size)
{
	size_t total = 1;  /* 前导 NUL（NULL 节区名） */
	size_t i;
	char *buf;
	size_t off;

	for (i = 0; i < w->section_count; ++i)
		total += strlen(w->sections[i].name) + 1;
	total += sizeof(".shstrtab");  /* 含 NUL */

	buf = malloc(total);
	if (!buf)
		return NULL;

	buf[0] = '\0';
	off = 1;
	for (i = 0; i < w->section_count; ++i) {
		size_t len = strlen(w->sections[i].name) + 1;
		name_offsets[i] = (uint32_t)off;
		memcpy(buf + off, w->sections[i].name, len);
		off += len;
	}
	name_offsets[w->section_count] = (uint32_t)off;
	memcpy(buf + off, ".shstrtab", sizeof(".shstrtab"));
	off += sizeof(".shstrtab");

	*out_size = total;
	return buf;
}

static uint64_t
align_up(uint64_t value, uint64_t align)
{
	if (align <= 1)
		return value;
	return (value + align - 1) & ~(align - 1);
}

int
mt_elf64_writer_finalize(struct mt_elf64_writer *w, FILE *out)
{
	/* 总节区数：0=NULL, 1..N=用户节区, N+1=.shstrtab */
	size_t total_sections = w->section_count + 2;
	uint16_t shstrtab_index = (uint16_t)(w->section_count + 1);
	uint32_t *name_offsets;
	char *shstrtab;
	size_t shstrtab_size;
	uint64_t *data_offsets;
	uint64_t offset;
	uint64_t shdr_offset;
	uint64_t file_size;
	unsigned char ehdr[MT_ELF64_EHDR_SIZE];
	size_t i;

	name_offsets = calloc(total_sections, sizeof(uint32_t));
	if (!name_offsets)
		return -1;

	shstrtab = build_shstrtab(w, name_offsets, &shstrtab_size);
	if (!shstrtab) {
		free(name_offsets);
		return -1;
	}

	data_offsets = calloc(total_sections, sizeof(uint64_t));
	if (!data_offsets) {
		free(shstrtab);
		free(name_offsets);
		return -1;
	}

	/* 计算布局 */
	offset = MT_ELF64_EHDR_SIZE;
	data_offsets[0] = 0;  /* NULL 节区 */

	for (i = 0; i < w->section_count; ++i) {
		struct mt_elf64_writer_section *s = &w->sections[i];
		offset = align_up(offset, s->alignment);
		data_offsets[i + 1] = offset;
		if (s->type != MT_SHT_NOBITS)
			offset += s->size;
	}

	/* .shstrtab 数据 */
	offset = align_up(offset, 1);
	data_offsets[shstrtab_index] = offset;
	offset += shstrtab_size;

	/* shdr 表 */
	offset = align_up(offset, 8);
	shdr_offset = offset;
	offset += total_sections * MT_ELF64_SHDR_SIZE;

	file_size = offset;

	/* 写 ehdr */
	memset(ehdr, 0, sizeof(ehdr));
	ehdr[0] = 0x7f;
	ehdr[1] = 'E';
	ehdr[2] = 'L';
	ehdr[3] = 'F';
	ehdr[4] = MT_ELFCLASS64;
	ehdr[5] = MT_ELFDATA2LSB;
	ehdr[6] = MT_EV_CURRENT;
	ehdr[7] = 0;  /* ELFOSABI_NONE */
	put16(ehdr + 16, w->type);
	put16(ehdr + 18, w->machine);
	put32(ehdr + 20, MT_EV_CURRENT);
	put64(ehdr + 24, w->entry);
	put64(ehdr + 32, 0);  /* phoff: 无程序头 */
	put64(ehdr + 40, shdr_offset);
	put32(ehdr + 48, w->flags);
	put16(ehdr + 52, MT_ELF64_EHDR_SIZE);
	put16(ehdr + 54, 0);  /* phentsize */
	put16(ehdr + 56, 0);  /* phnum */
	put16(ehdr + 58, MT_ELF64_SHDR_SIZE);
	put16(ehdr + 60, (uint16_t)total_sections);
	put16(ehdr + 62, shstrtab_index);

	if (fwrite(ehdr, 1, sizeof(ehdr), out) != sizeof(ehdr))
		goto io_err;

	/* 写节区数据 */
	for (i = 0; i < w->section_count; ++i) {
		struct mt_elf64_writer_section *s = &w->sections[i];
		if (s->type == MT_SHT_NOBITS || s->size == 0)
			continue;
		if (fseek(out, (long)data_offsets[i + 1], SEEK_SET) != 0)
			goto io_err;
		if (fwrite(s->data, 1, s->size, out) != s->size)
			goto io_err;
	}

	/* 写 .shstrtab 数据 */
	if (fseek(out, (long)data_offsets[shstrtab_index], SEEK_SET) != 0)
		goto io_err;
	if (fwrite(shstrtab, 1, shstrtab_size, out) != shstrtab_size)
		goto io_err;

	/* 写 shdr 表 */
	if (fseek(out, (long)shdr_offset, SEEK_SET) != 0)
		goto io_err;
	for (i = 0; i < total_sections; ++i) {
		unsigned char shdr[MT_ELF64_SHDR_SIZE];
		memset(shdr, 0, sizeof(shdr));

		if (i == 0) {
			/* NULL 节区：全零 */
		} else if (i == shstrtab_index) {
			put32(shdr + 0, name_offsets[w->section_count]);
			put32(shdr + 4, MT_SHT_STRTAB);
			put64(shdr + 24, data_offsets[shstrtab_index]);
			put64(shdr + 32, shstrtab_size);
			put64(shdr + 48, 1);  /* align */
		} else {
			struct mt_elf64_writer_section *s = &w->sections[i - 1];
			put32(shdr + 0, name_offsets[i - 1]);
			put32(shdr + 4, s->type);
			put64(shdr + 8, s->flags);
			put64(shdr + 16, s->address);
			put64(shdr + 24, data_offsets[i]);
			put64(shdr + 32, s->size);
			put32(shdr + 40, s->link);
			put32(shdr + 44, s->info);
			put64(shdr + 48, s->alignment);
			put64(shdr + 56, s->entry_size);
		}
		if (fwrite(shdr, 1, sizeof(shdr), out) != sizeof(shdr))
			goto io_err;
	}

	/* 确保文件大小正确 */
	if (fseek(out, (long)file_size, SEEK_SET) != 0)
		goto io_err;

	free(shstrtab);
	free(name_offsets);
	free(data_offsets);
	return 0;

io_err:
	free(shstrtab);
	free(name_offsets);
	free(data_offsets);
	return -1;
}
