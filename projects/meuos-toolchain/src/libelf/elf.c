/* elf.c - MeuOS Toolchain ELF64 little-endian reader. */
#include "mt/elf.h"

#include <limits.h>
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

	if (!p || !view)
		return MT_ELF_E_ARGUMENT;
	if (size < MT_ELF64_EHDR_SIZE)
		return MT_ELF_E_TRUNCATED;
	if (memcmp(p, "\177ELF", 4) != 0)
		return MT_ELF_E_MAGIC;
	if (p[4] != MT_ELFCLASS64)
		return MT_ELF_E_CLASS;
	if (p[5] != MT_ELFDATA2LSB)
		return MT_ELF_E_ENCODING;
	if (p[6] != MT_EV_CURRENT || read32(p + 20) != MT_EV_CURRENT)
		return MT_ELF_E_VERSION;

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
