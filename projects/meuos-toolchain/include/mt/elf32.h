#ifndef MT_ELF32_H
#define MT_ELF32_H

/* ELF32 structures and API for the i386 target.
 *
 * All on-disk fields are little-endian.  The structures are read-only
 * views over an existing buffer — the caller owns the data.
 *
 * ELF32 header is 52 bytes, section header is 40 bytes, symbol is 16 bytes,
 * RELA is 12 bytes (REL is 8), program header is 32 bytes. */

#include <stddef.h>
#include <stdint.h>

#define MT_ELF32_EHDR_SIZE 52
#define MT_ELF32_PHDR_SIZE 32
#define MT_ELF32_SHDR_SIZE 40
#define MT_ELF32_SYM_SIZE  16

struct mt_elf32_view {
	uint16_t type;
	uint16_t machine;
	uint32_t version;
	uint32_t entry;              /* 32-bit */
	uint32_t program_offset;     /* 32-bit */
	uint32_t section_offset;     /* 32-bit */
	uint32_t flags;
	uint16_t header_size;
	uint16_t program_entry_size;
	uint16_t program_count;
	uint16_t section_entry_size;
	uint16_t section_count;
	uint16_t section_name_index;
};

struct mt_elf32_section {
	uint32_t name;
	uint32_t type;
	uint32_t flags;              /* 32-bit */
	uint32_t address;            /* 32-bit */
	uint32_t offset;             /* 32-bit */
	uint32_t size;               /* 32-bit */
	uint32_t link;
	uint32_t info;
	uint32_t alignment;          /* 32-bit */
	uint32_t entry_size;         /* 32-bit */
};

struct mt_elf32_symbol {
	uint32_t name;
	uint8_t  info;
	uint8_t  other;
	uint16_t section;
	uint32_t value;              /* 32-bit */
	uint32_t size;               /* 32-bit */
};

struct mt_elf32_phdr {
	uint32_t type;
	uint32_t offset;             /* 32-bit */
	uint32_t vaddr;              /* 32-bit */
	uint32_t paddr;              /* 32-bit */
	uint32_t filesz;             /* 32-bit */
	uint32_t memsz;              /* 32-bit */
	uint32_t flags;
	uint32_t align;              /* 32-bit */
};

struct mt_elf32_rel {
	uint32_t offset;
	uint32_t info;
};

struct mt_elf32_rela {
	uint32_t offset;
	uint32_t info;
	int32_t  addend;
};

/* Helper macros */
#define MT_ELF32_R_SYM(i)  ((i) >> 8)
#define MT_ELF32_R_TYPE(i) ((uint8_t)((i) & 0xff))
#define MT_ELF32_R_INFO(s, t) (((s) << 8) | (uint8_t)(t))

#define MT_ELF32_ST_BIND(i)  ((i) >> 4)
#define MT_ELF32_ST_TYPE(i)  ((i) & 0xf)
#define MT_ELF32_ST_INFO(b, t) (((b) << 4) | ((t) & 0xf))

/* ---- ELF32 parser API ---- */

enum mt_elf_status mt_elf32_parse(const void *bytes, size_t size,
                                  struct mt_elf32_view *view);

enum mt_elf_status mt_elf32_get_section(const void *bytes, size_t size,
                                        const struct mt_elf32_view *view,
                                        uint16_t index,
                                        struct mt_elf32_section *section);

enum mt_elf_status mt_elf32_get_symbol(const void *bytes, size_t size,
                                       const struct mt_elf32_section *table,
                                       uint64_t index,
                                       struct mt_elf32_symbol *symbol);

enum mt_elf_status mt_elf32_get_string(const void *bytes, size_t size,
                                       const struct mt_elf32_section *strings,
                                       uint32_t offset,
                                       const char **value);

enum mt_elf_status mt_elf32_get_phdr(const void *bytes, size_t size,
                                     const struct mt_elf32_view *view,
                                     uint16_t index,
                                     struct mt_elf32_phdr *phdr);

enum mt_elf_status mt_elf32_get_rela(const void *bytes, size_t size,
                                     const struct mt_elf32_section *table,
                                     uint64_t index,
                                     struct mt_elf32_rela *rela);

enum mt_elf_status mt_elf32_find_section(const void *bytes, size_t size,
                                         const struct mt_elf32_view *view,
                                         const char *name,
                                         struct mt_elf32_section *section);

/* ---- ELF32 writer API ---- */

struct mt_elf32_writer_section {
	const char *name;
	uint32_t type;
	uint32_t flags;
	uint32_t address;
	const void *data;
	uint32_t size;
	uint32_t alignment;
	uint32_t link;
	uint32_t info;
	uint32_t entry_size;
};

struct mt_elf32_writer {
	uint16_t machine;
	uint16_t type;
	uint32_t entry;
	uint32_t flags;
	struct mt_elf32_writer_section *sections;
	size_t section_count;
	size_t section_capacity;
};

void mt_elf32_writer_init(struct mt_elf32_writer *w, uint16_t machine,
                          uint16_t type);
void mt_elf32_writer_free(struct mt_elf32_writer *w);
int  mt_elf32_writer_add_section(struct mt_elf32_writer *w,
                                 const struct mt_elf32_writer_section *sec);
void mt_elf32_writer_set_entry(struct mt_elf32_writer *w, uint32_t entry);
void mt_elf32_writer_set_flags(struct mt_elf32_writer *w, uint32_t flags);
int  mt_elf32_writer_finalize(struct mt_elf32_writer *w, FILE *out);

#endif /* MT_ELF32_H */
