/* link.c - static ET_REL -> ET_EXEC linker. */
#include "mt/ld.h"
#include "mt/archive.h"
#include "mt/elf.h"
#include "mt/elf32.h"
#include "mt/target.h"
#include "mt/msys.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Per-architecture relocation application */
extern int mt_apply_aarch64_reloc(unsigned type, unsigned char *loc,
                                  uint64_t S, int64_t A, uint64_t P);
extern int la64_apply_reloc(unsigned type, unsigned char *loc,
                            uint64_t S, int64_t A, uint64_t P);
extern int riscv64_apply_reloc(unsigned reloc_type, unsigned char *place,
                               uint64_t S, int64_t A, uint64_t P);
extern int i386_apply_reloc(unsigned reloc_type, unsigned char *place,
                            uint64_t S, int64_t A, uint64_t P);
extern int mt_apply_arm_reloc(unsigned type, unsigned char *loc,
                              uint64_t S, int64_t A, uint64_t P);

/* msys VFS support: globals from main.c */
extern struct msys *ld_msys;
extern int msys_vfs_load(const char *path, void **buf, size_t *size);

#define LD_SHF_WRITE 0x1ULL
#define LD_SHF_ALLOC 0x2ULL
#define LD_SHF_EXECINSTR 0x4ULL
#define LD_PT_LOAD 1
#define LD_PF_X 1
#define LD_PF_W 2
#define LD_PF_R 4
#define LD_PAGE 0x1000ULL
#define LD_BASE 0x400000ULL
#define LD_SHN_COMMON 0xfff2
#define LD_STB_LOCAL 0
#define LD_STB_GLOBAL 1
#define LD_STB_WEAK 2
#define LD_STB_SHIFT 4
#define LD_R_X86_64_64 1
#define LD_R_X86_64_PC32 2
#define LD_R_X86_64_PLT32 4
#define LD_R_X86_64_GOTPCREL 9
#define LD_R_X86_64_32 10
#define LD_R_X86_64_32S 11
#define LD_R_X86_64_TPOFF32 23

struct ld_group {
	char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t align;
	unsigned char *data;
	size_t size;
	size_t capacity;
	uint64_t file_offset;
	uint64_t address;
	int rank;
	int kept;      /* 1 = reachable from roots (for --gc-sections) */
};

struct ld_section_map {
	int group;
	uint64_t offset;
};

struct ld_object {
	char *name;
	unsigned char *data;
	size_t size;
	int elf_class;
	union {
		struct mt_elf64_view v64;
		struct mt_elf32_view v32;
	} view;
	struct ld_section_map *maps;
	uint16_t symtab_index;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} symtab;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} strtab;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} section_names;
	int has_symtab;
};

struct ld_objects {
	struct ld_object *items;
	size_t count;
	size_t capacity;
};

struct ld_archives {
	char **paths;
	size_t count;
	size_t capacity;
};

struct ld_global {
	char *name;
	struct ld_object *object;
	uint64_t symbol_index;
	int group;
	uint64_t offset;
	uint64_t size;
	uint64_t align;
	int defined;
	int weak;
	int common;
	int absolute;  /* 1 = value is an absolute address (from --defsym) */
	struct ld_global *alias; /* non-NULL: this global redirects to alias target
	                         * (used by --wrap). symbol_value() and other
	                         * resolvers follow the alias chain automatically. */
};

struct ld_globals {
	struct ld_global *items;
	size_t count;
	size_t capacity;
};

struct ld_got_entry {
	char *name;
	uint64_t offset;
};

struct ld_got {
	struct ld_got_entry *items;
	size_t count;
	size_t capacity;
	int group;
};

/* One exported symbol recorded for the dynamic symbol table (.dynsym).
 * The actual .dynsym entry bytes are filled in after layout (when the final
 * virtual addresses are known), so we keep the metadata here. */
struct ld_dynsym_entry {
	struct ld_global *global;  /* back-pointer into ctx->globals */
	uint32_t dynstr_offset;    /* offset of the name within .dynstr */
	int stt;                   /* STT_FUNC / STT_OBJECT / STT_TLS */
};

struct ld_context {
	const struct mt_target *target;
	struct ld_objects objects;
	struct ld_archives archives;
	struct ld_group *groups;
	size_t group_count;
	size_t group_capacity;
	struct ld_globals globals;
	struct ld_got got;
	int tls_tdata_group;
	int tls_tbss_group;
	uint64_t tls_tdata_size;
	uint64_t tls_size;
	uint64_t tls_align;
	int shared;          /* 1 = ET_DYN (shared library), 0 = ET_EXEC */
	int pie;             /* 1 = PIE (ET_DYN + PT_INTERP) */
	int build_id;        /* 1 = generate .note.gnu.build-id */
	int eh_frame_hdr;    /* 1 = generate .eh_frame_hdr */
	int whole_archive;   /* 1 = force-extract all archive members */
	int as_needed;        /* 1 = --as-needed, 0 = --no-as-needed */
	int no_undefined;    /* 1 = error on undefined symbols */
	int gc_sections;     /* 1 = garbage-collect unused sections */
	int print_map;       /* 1 = output link map */
	const char *link_script; /* path to section layout script */
	const char *soname;  /* DT_SONAME for shared lib (may be NULL) */
	/* Dynamic symbol table bookkeeping (shared libs only) */
	struct ld_dynsym_entry *dynsym_entries;
	size_t dynsym_count;
	size_t dynsym_capacity;
	uint64_t dynsym_data_offset;   /* offset of first entry within .dynsym group */
	uint32_t soname_dynstr_offset; /* .dynstr offset of DT_SONAME string (0 if none) */
	/* In-memory archive data (for VFS .msys archives) */
	unsigned char **archive_mem_data;
	size_t *archive_mem_size;
	size_t archive_mem_count;
	size_t archive_mem_capacity;
	/* Linker options (copied from struct mt_ld_options) */
	const char *output;     /* output file path */
	const char *entry;      /* entry symbol (default "_start") */
	char error[512];
};

static void *
ld_malloc(size_t size)
{
	void *p = malloc(size == 0 ? 1 : size);
	return p;
}

static void *
ld_realloc(void *old, size_t size)
{
	return realloc(old, size == 0 ? 1 : size);
}

static char *
ld_strdup(const char *text)
{
	size_t n = strlen(text);
	char *copy = (char *)ld_malloc(n + 1);
	if (copy)
		memcpy(copy, text, n + 1);
	return copy;
}

static int
ld_error(struct ld_context *ctx, const char *message)
{
	strncpy(ctx->error, message, sizeof(ctx->error) - 1);
	ctx->error[sizeof(ctx->error) - 1] = '\0';
	return -1;
}

static int
ld_errorf(struct ld_context *ctx, const char *prefix, const char *name)
{
	if (name)
		snprintf(ctx->error, sizeof(ctx->error), "%s: %s", prefix, name);
	else
		ld_error(ctx, prefix);
	return -1;
}

static uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t
read16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint64_t
read64(const unsigned char *p)
{
	return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}

static void
write16(unsigned char *p, uint16_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
}

static void
write32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
	p[2] = (unsigned char)(value >> 16);
	p[3] = (unsigned char)(value >> 24);
}

static void
write64(unsigned char *p, uint64_t value)
{
	unsigned i;
	for (i = 0; i < 8; ++i)
		p[i] = (unsigned char)(value >> (i * 8));
}

static uint64_t
align_up(uint64_t value, uint64_t align)
{
	uint64_t mask = align - 1;
	return (value + mask) & ~mask;
}

/* ---- ELF32/64 dispatch helpers ---- */

/* Forward declarations for functions used by helpers below. */
static struct ld_global *find_global(struct ld_context *ctx, const char *name);
static int ld_errorf(struct ld_context *ctx, const char *prefix, const char *name);
static int ld_error(struct ld_context *ctx, const char *message);

/* Parse an object file's ELF header.  Returns 0 on success, -1 on failure. */
static int
object_parse(struct ld_context *ctx, struct ld_object *object)
{
	enum mt_elf_status status;
	if (ctx->target->elf_class == 1) {
		status = mt_elf32_parse(object->data, object->size, &object->view.v32);
		if (status != MT_ELF_OK || object->view.v32.type != MT_ET_REL ||
		    object->view.v32.machine != ctx->target->emachine)
			return -1;
	} else {
		status = mt_elf64_parse(object->data, object->size, &object->view.v64);
		if (status != MT_ELF_OK || object->view.v64.type != MT_ET_REL ||
		    object->view.v64.machine != ctx->target->emachine)
			return -1;
	}
	object->elf_class = ctx->target->elf_class;
	return 0;
}

/* Section count accessor — works for both 32 and 64 bit views. */
static uint16_t
object_section_count(const struct ld_object *object)
{
	return (object->elf_class == 1)
		? object->view.v32.section_count
		: object->view.v64.section_count;
}

/* Section name index accessor. */
static uint16_t
object_section_name_index(const struct ld_object *object)
{
	return (object->elf_class == 1)
		? object->view.v32.section_name_index
		: object->view.v64.section_name_index;
}

/* Read a section header and convert to 64-bit fields. */
static int
object_get_section(struct ld_object *object, uint16_t index,
                   struct mt_elf64_section *out)
{
	if (object->elf_class == 1) {
		struct mt_elf32_section s32;
		if (mt_elf32_get_section(object->data, object->size,
		                         &object->view.v32, index, &s32) != MT_ELF_OK)
			return -1;
		out->name = s32.name;
		out->type = s32.type;
		out->flags = s32.flags;
		out->address = s32.address;
		out->offset = s32.offset;
		out->size = s32.size;
		out->link = s32.link;
		out->info = s32.info;
		out->alignment = s32.alignment;
		out->entry_size = s32.entry_size;
	} else {
		if (mt_elf64_get_section(object->data, object->size,
		                         &object->view.v64, index, out) != MT_ELF_OK)
			return -1;
	}
	return 0;
}

/* Read a symbol and convert to 64-bit fields. */
static int
object_get_symbol(struct ld_object *object, uint64_t index,
                  struct mt_elf64_symbol *out)
{
	if (object->elf_class == 1) {
		struct mt_elf32_symbol sym32;
		if (mt_elf32_get_symbol(object->data, object->size,
		                        &object->symtab.v32, index,
		                        &sym32) != MT_ELF_OK)
			return -1;
		out->name = sym32.name;
		out->info = sym32.info;
		out->other = sym32.other;
		out->section = sym32.section;
		out->value = sym32.value;
		out->size = sym32.size;
	} else {
		if (mt_elf64_get_symbol(object->data, object->size,
		                        &object->symtab.v64, index, out) != MT_ELF_OK)
			return -1;
	}
	return 0;
}

/* Read a string from the symbol string table (strtab). */
static int
object_get_strtab_string(struct ld_object *object, uint32_t offset,
                         const char **value)
{
	if (object->elf_class == 1)
		return mt_elf32_get_string(object->data, object->size,
		                            &object->strtab.v32, offset, value);
	return mt_elf64_get_string(object->data, object->size,
	                            &object->strtab.v64, offset, value);
}

/* Read a string from the section-name string table (section_names). */
static int
object_get_secname_string(struct ld_object *object, uint32_t offset,
                          const char **value)
{
	if (object->elf_class == 1)
		return mt_elf32_get_string(object->data, object->size,
		                            &object->section_names.v32, offset, value);
	return mt_elf64_get_string(object->data, object->size,
	                            &object->section_names.v64, offset, value);
}

/* Read a section name (calls object_get_section + gets name string). */
static const char *
object_section_name(struct ld_object *object, uint16_t index)
{
	struct mt_elf64_section section;
	const char *name;
	if (object_get_section(object, index, &section) != 0)
		return NULL;
	if (object_get_secname_string(object, section.name, &name) != 0)
		return NULL;
	return name;
}

/* Helper: check if an archive member ELF contains a needed undefined symbol.
 * elf_class is ctx->target->elf_class (1 for ELF32, 2 for ELF64). */
static int
archive_member_needed_impl(struct ld_context *ctx, const unsigned char *data,
                      size_t size, int elf_class)
{
	union {
		struct mt_elf64_view v64;
		struct mt_elf32_view v32;
	} view;
	union {
		struct mt_elf64_section v64;
		struct mt_elf32_section v32;
	} symtab, strtab;
	struct mt_elf64_symbol symbol;
	const char *name;
	uint16_t i;
	uint64_t j;
	uint16_t section_count;
	unsigned binding;

	/* Parse based on elf_class */
	if (elf_class == 1) {
		if (mt_elf32_parse(data, size, &view.v32) != MT_ELF_OK ||
		    view.v32.type != MT_ET_REL ||
		    view.v32.machine != ctx->target->emachine)
			return 0;
		section_count = view.v32.section_count;
	} else {
		if (mt_elf64_parse(data, size, &view.v64) != MT_ELF_OK ||
		    view.v64.type != MT_ET_REL ||
		    view.v64.machine != ctx->target->emachine)
			return 0;
		section_count = view.v64.section_count;
	}

	for (i = 0; i < section_count; ++i) {
		if (elf_class == 1) {
			struct mt_elf32_section s32;
			if (mt_elf32_get_section(data, size, &view.v32, i, &s32) != MT_ELF_OK)
				return -1;
			if (s32.type != MT_SHT_SYMTAB)
				continue;
			symtab.v32 = s32;
			if (s32.link >= section_count ||
			    mt_elf32_get_section(data, size, &view.v32,
			                         (uint16_t)s32.link, &strtab.v32) != MT_ELF_OK)
				return -1;
			for (j = 0; j < s32.size / s32.entry_size; ++j) {
				struct mt_elf32_symbol sym32;
				struct ld_global *global;
				if (mt_elf32_get_symbol(data, size, &symtab.v32, j,
				                        &sym32) != MT_ELF_OK)
					return -1;
				binding = sym32.info >> LD_STB_SHIFT;
				if (binding == LD_STB_LOCAL || sym32.section == MT_SHN_UNDEF ||
				    sym32.name == 0 ||
				    mt_elf32_get_string(data, size, &strtab.v32, sym32.name,
				                       &name) != MT_ELF_OK)
					continue;
				global = find_global(ctx, name);
				if (global && !global->defined)
					return 1;
			}
		} else {
			struct mt_elf64_section s64;
			if (mt_elf64_get_section(data, size, &view.v64, i, &s64) != MT_ELF_OK)
				return -1;
			if (s64.type != MT_SHT_SYMTAB)
				continue;
			symtab.v64 = s64;
			if (s64.link >= section_count ||
			    mt_elf64_get_section(data, size, &view.v64,
			                         (uint16_t)s64.link, &strtab.v64) != MT_ELF_OK)
				return -1;
			for (j = 0; j < s64.size / s64.entry_size; ++j) {
				struct ld_global *global;
				if (mt_elf64_get_symbol(data, size, &symtab.v64, j,
				                        &symbol) != MT_ELF_OK)
					return -1;
				binding = symbol.info >> LD_STB_SHIFT;
				if (binding == LD_STB_LOCAL || symbol.section == MT_SHN_UNDEF ||
				    symbol.name == 0 ||
				    mt_elf64_get_string(data, size, &strtab.v64, symbol.name,
				                       &name) != MT_ELF_OK)
					continue;
				global = find_global(ctx, name);
				if (global && !global->defined)
					return 1;
			}
		}
		break;
	}
	return 0;
}

static void
free_object(struct ld_object *object)
{
	free(object->name);
	free(object->data);
	free(object->maps);
	memset(object, 0, sizeof(*object));
}

static void
free_context(struct ld_context *ctx)
{
	size_t i;
	for (i = 0; i < ctx->objects.count; ++i)
		free_object(&ctx->objects.items[i]);
	for (i = 0; i < ctx->group_count; ++i) {
		free(ctx->groups[i].name);
		free(ctx->groups[i].data);
	}
	for (i = 0; i < ctx->globals.count; ++i)
		free(ctx->globals.items[i].name);
	for (i = 0; i < ctx->got.count; ++i)
		free(ctx->got.items[i].name);
	for (i = 0; i < ctx->archives.count; ++i)
		free(ctx->archives.paths[i]);
	free(ctx->archives.paths);
	free(ctx->objects.items);
	free(ctx->groups);
	free(ctx->globals.items);
	free(ctx->got.items);
	free(ctx->dynsym_entries);
	/* Free in-memory archive data (from .msys VFS) */
	for (i = 0; i < ctx->archive_mem_count; ++i)
		free(ctx->archive_mem_data[i]);
	free(ctx->archive_mem_data);
	free(ctx->archive_mem_size);
	memset(ctx, 0, sizeof(*ctx));
}

static int
append_object(struct ld_context *ctx, const char *name,
              const unsigned char *data, size_t size)
{
	struct ld_object *objects;
	struct ld_object *object;
	if (ctx->objects.count == ctx->objects.capacity) {
		size_t capacity = ctx->objects.capacity ? ctx->objects.capacity * 2 : 16;
		objects = (struct ld_object *)ld_realloc(
		    ctx->objects.items, capacity * sizeof(*objects));
		if (!objects)
			return ld_error(ctx, "out of memory");
		ctx->objects.items = objects;
		ctx->objects.capacity = capacity;
	}
	object = &ctx->objects.items[ctx->objects.count];
	memset(object, 0, sizeof(*object));
	object->name = ld_strdup(name);
	object->data = (unsigned char *)ld_malloc(size);
	if (!object->name || !object->data) {
		free_object(object);
		return ld_error(ctx, "out of memory");
	}
	memcpy(object->data, data, size);
	object->size = size;
	if (object_parse(ctx, object) != 0) {
		free_object(object);
		return ld_errorf(ctx, "unsupported input object", name);
	}
	{
		uint16_t sec_count = object_section_count(object);
		object->maps = (struct ld_section_map *)ld_malloc(
		    sec_count * sizeof(*object->maps));
		if (!object->maps && sec_count != 0) {
			free_object(object);
			return ld_error(ctx, "out of memory");
		}
		for (uint16_t i = 0; i < sec_count; ++i)
			object->maps[i].group = -1;
	}
	{
		uint16_t sni = object_section_name_index(object);
		if (object->elf_class == 1) {
			struct mt_elf32_section s32;
			if (sni >= object_section_count(object) ||
			    mt_elf32_get_section(object->data, object->size,
			                         &object->view.v32, sni,
			                         &s32) != MT_ELF_OK) {
				free_object(object);
				return ld_errorf(ctx, "invalid section-name table", name);
			}
			object->section_names.v32 = s32;
		} else {
			if (sni >= object_section_count(object) ||
			    mt_elf64_get_section(object->data, object->size,
			                         &object->view.v64, sni,
			                         &object->section_names.v64) != MT_ELF_OK) {
				free_object(object);
				return ld_errorf(ctx, "invalid section-name table", name);
			}
		}
	}
	++ctx->objects.count;
	return 0;
}

static int
read_file(const char *path, unsigned char **data, size_t *size)
{
	FILE *file;
	long length;
	unsigned char *buffer;

	/* Try regular fopen first */
	file = fopen(path, "rb");
	if (file) {
		if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
		    fseek(file, 0, SEEK_SET) != 0 || (uint64_t)length > SIZE_MAX) {
			fclose(file);
			return -1;
		}
		buffer = (unsigned char *)ld_malloc((size_t)length);
		if (!buffer || (length != 0 && fread(buffer, 1, (size_t)length, file) !=
		                (size_t)length)) {
			free(buffer);
			fclose(file);
			return -1;
		}
		if (fclose(file) != 0) {
			free(buffer);
			return -1;
		}
		*data = buffer;
		*size = (size_t)length;
		return 0;
	}

	/* Fallback: try .msys VFS for @msys: paths */
	if (strncmp(path, "@msys:", 6) == 0) {
		void *msys_buf = NULL;
		size_t msys_sz = 0;
		if (msys_vfs_load(path, &msys_buf, &msys_sz) > 0) {
			*data = (unsigned char *)msys_buf;
			if (size) *size = msys_sz;
			return 0;
		}
	}

	return -1;
}

static int
remember_archive(struct ld_context *ctx, const char *path)
{
	char **paths;
	if (ctx->archives.count == ctx->archives.capacity) {
		size_t capacity = ctx->archives.capacity ? ctx->archives.capacity * 2 : 8;
		paths = (char **)ld_realloc(ctx->archives.paths,
		                           capacity * sizeof(*paths));
		if (!paths)
			return ld_error(ctx, "out of memory");
		ctx->archives.paths = paths;
		ctx->archives.capacity = capacity;
	}
	ctx->archives.paths[ctx->archives.count] = ld_strdup(path);
	if (!ctx->archives.paths[ctx->archives.count])
		return ld_error(ctx, "out of memory");
	++ctx->archives.count;
	return 0;
}

static int
load_input(struct ld_context *ctx, const char *path)
{
	unsigned char *data;
	size_t size;
	const char *suffix = strrchr(path, '.');
	if (suffix && strcmp(suffix, ".a") == 0) {
		/* For .a files from msys VFS, read the data and store it in-memory
		 * so extract_archives can use mt_ar_foreach_mem(). */
		if (strncmp(path, "@msys:", 6) == 0) {
			void *msys_buf = NULL;
			size_t msys_sz = 0;
			if (msys_vfs_load(path, &msys_buf, &msys_sz) <= 0)
				return ld_errorf(ctx, "cannot read archive from .msys", path);
			/* Store in in-memory archive array */
			if (ctx->archive_mem_count == ctx->archive_mem_capacity) {
				size_t cap = ctx->archive_mem_capacity
					? ctx->archive_mem_capacity * 2 : 8;
				unsigned char **d = (unsigned char **)ld_realloc(
					ctx->archive_mem_data, cap * sizeof(*d));
				size_t *s = (size_t *)ld_realloc(
					ctx->archive_mem_size, cap * sizeof(*s));
				if (!d || !s) {
					free(d); free(s);
					return ld_error(ctx, "out of memory");
				}
				ctx->archive_mem_data = d;
				ctx->archive_mem_size = s;
				ctx->archive_mem_capacity = cap;
			}
			ctx->archive_mem_data[ctx->archive_mem_count] =
				(unsigned char *)msys_buf;
			ctx->archive_mem_size[ctx->archive_mem_count] = msys_sz;
			ctx->archive_mem_count++;
			return remember_archive(ctx, path);
		}
		return remember_archive(ctx, path);
	}
	if (read_file(path, &data, &size) != 0)
		return ld_errorf(ctx, "cannot read input", path);
	if (append_object(ctx, path, data, size) != 0) {
		free(data);
		return -1;
	}
	free(data);
	return 0;
}

static int
section_rank(const char *name)
{
	if (strcmp(name, ".text") == 0) return 0;
	if (strncmp(name, ".text.", 6) == 0) return 0;
	if (strcmp(name, ".rodata") == 0) return 1;
	if (strcmp(name, ".interp") == 0) return 1;
	if (strcmp(name, ".eh_frame") == 0) return 1;
	if (strcmp(name, ".note.gnu.build-id") == 0) return 1;
	if (strcmp(name, ".dynsym") == 0) return 1;
	if (strcmp(name, ".dynstr") == 0) return 1;
	if (strcmp(name, ".hash") == 0) return 1;
	if (strcmp(name, ".got") == 0) return 2;
	if (strcmp(name, ".dynamic") == 0) return 2;
	if (strcmp(name, ".data") == 0 || strcmp(name, ".tdata") == 0) return 3;
	if (strncmp(name, ".data.", 6) == 0) return 3;
	if (strcmp(name, ".bss") == 0 || strcmp(name, ".tbss") == 0) return 4;
	/* Debug and other non-allocatable sections go after ALL loaded sections */
	if (strncmp(name, ".debug", 6) == 0) return 5;
	if (strncmp(name, ".note", 5) == 0) return 5;
	if (strncmp(name, ".comment", 8) == 0) return 5;
	return 6;
}

static int
is_tls_group(struct ld_context *ctx, int group)
{
	return group == ctx->tls_tdata_group || group == ctx->tls_tbss_group;
}

static int
find_group(struct ld_context *ctx, const char *name)
{
	size_t i;
	for (i = 0; i < ctx->group_count; ++i)
		if (strcmp(ctx->groups[i].name, name) == 0)
			return (int)i;
	return -1;
}

static int
get_group(struct ld_context *ctx, const char *name, uint32_t type,
          uint64_t flags, uint64_t align)
{
	struct ld_group *groups;
	int index = find_group(ctx, name);
	if (index >= 0) {
		if (ctx->groups[index].align < align)
			ctx->groups[index].align = align;
		return index;
	}
	if (ctx->group_count == ctx->group_capacity) {
		size_t capacity = ctx->group_capacity ? ctx->group_capacity * 2 : 8;
		groups = (struct ld_group *)ld_realloc(ctx->groups,
		                                      capacity * sizeof(*groups));
		if (!groups)
			return -1;
		ctx->groups = groups;
		ctx->group_capacity = capacity;
	}
	index = (int)ctx->group_count++;
	memset(&ctx->groups[index], 0, sizeof(ctx->groups[index]));
	ctx->groups[index].name = ld_strdup(name);
	if (!ctx->groups[index].name)
		return -1;
	ctx->groups[index].type = type;
	ctx->groups[index].flags = flags;
	ctx->groups[index].align = align ? align : 1;
	ctx->groups[index].rank = section_rank(name);
	return index;
}

static int
append_group_data(struct ld_context *ctx, struct ld_group *group,
                  const unsigned char *data, size_t size, uint64_t align,
                  uint64_t *section_offset)
{
	uint64_t aligned = align_up(group->size, align ? align : 1);
	size_t need;
	unsigned char *buffer;
	if (aligned > SIZE_MAX || size > SIZE_MAX - (size_t)aligned) {
		ld_error(ctx, "output section overflow");
		return -1;
	}
	need = (size_t)aligned + size;
	if (need > group->capacity) {
		size_t capacity = group->capacity ? group->capacity * 2 : 256;
		while (capacity < need) {
			if (capacity > SIZE_MAX / 2) {
				capacity = need;
				break;
			}
			capacity *= 2;
		}
		buffer = (unsigned char *)ld_realloc(group->data, capacity);
		if (!buffer)
			return ld_error(ctx, "out of memory");
		if (capacity > group->capacity)
			memset(buffer + group->capacity, 0, capacity - group->capacity);
		group->data = buffer;
		group->capacity = capacity;
	}
	if (group->type != MT_SHT_NOBITS && size != 0) {
		if (data)
			memcpy(group->data + aligned, data, size);
		else
			memset(group->data + aligned, 0, size);
	}
	group->size = need;
	*section_offset = aligned;
	return 0;
}

static int
collect_sections(struct ld_context *ctx)
{
	size_t i;
	uint16_t j;
	struct mt_elf64_section section;
	const char *name;
	struct ld_group *out;
	int group;
	uint64_t offset;

	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, j, &section) != 0)
				return ld_errorf(ctx, "invalid section in object", object->name);
			if (section.type != MT_SHT_PROGBITS &&
			    section.type != MT_SHT_NOBITS)
				continue;
			/* Allocatable and non-allocatable PROGBITS sections
			 * (e.g. .debug_*) are both collected.  Non-allocatable
			 * sections get rank 5+ and are placed after loaded data. */
			name = object_section_name(object, j);
			if (!name || !*name)
				return ld_errorf(ctx, "section has no name", object->name);
			if (section.size > SIZE_MAX ||
			    (section.type != MT_SHT_NOBITS && section.offset > SIZE_MAX))
				return ld_error(ctx, "section is too large");
			group = get_group(ctx, name, section.type, section.flags,
			                  section.alignment ? section.alignment : 1);
			if (group < 0)
				return ld_error(ctx, "out of memory");
			out = &ctx->groups[group];
			if (section.type == MT_SHT_NOBITS) {
				if (append_group_data(ctx, out, NULL, (size_t)section.size,
				                      section.alignment ? section.alignment : 1,
				                      &offset) != 0)
					return -1;
			} else if (append_group_data(ctx, out,
				                      object->data + section.offset,
				                      (size_t)section.size,
				                      section.alignment ? section.alignment : 1,
				                      &offset) != 0) {
				return -1;
			}
			object->maps[j].group = group;
			object->maps[j].offset = offset;
		}
	}
	return 0;
}

static int
collect_one_object_sections(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_section section;
	const char *name;
	struct ld_group *out;
	uint16_t j;
	int group;
	uint64_t offset;
	for (j = 0; j < object_section_count(object); ++j) {
		if (object_get_section(object, j, &section) != 0)
			return ld_errorf(ctx, "invalid section in object", object->name);
		if (section.type != MT_SHT_PROGBITS && section.type != MT_SHT_NOBITS)
			continue;
		name = object_section_name(object, j);
		if (!name || !*name)
			return ld_errorf(ctx, "section has no name", object->name);
		group = get_group(ctx, name, section.type, section.flags,
		                  section.alignment ? section.alignment : 1);
		if (group < 0)
			return ld_error(ctx, "out of memory");
		if (strncmp(name, ".debug", 6) == 0)
			fprintf(stderr, "DEBUG collect: section[%u]=%s group=%d\n", j, name, group);
		out = &ctx->groups[group];
		/* Set up group rank and flags based on section attributes */
		if (section.flags & LD_SHF_ALLOC) {
			/* Allocatable sections keep their original attributes */
		} else {
			/* Non-allocatable sections (debug, comments, etc.) go at
			 * the end of the file, after all loadable content.  They
			 * are NOT included in PT_LOAD segments. */
			ctx->groups[group].flags = section.flags;
		}
		if (section.type == MT_SHT_NOBITS) {
			if (append_group_data(ctx, out, NULL, (size_t)section.size,
			                      section.alignment ? section.alignment : 1,
			                      &offset) != 0)
				return -1;
		} else if (append_group_data(ctx, out,
		                      object->data + section.offset,
		                      (size_t)section.size,
		                      section.alignment ? section.alignment : 1,
		                      &offset) != 0)
			return -1;
		object->maps[j].group = group;
		object->maps[j].offset = offset;
	}
	return 0;
}

static struct ld_global *
find_global(struct ld_context *ctx, const char *name)
{
	size_t i;
	for (i = 0; i < ctx->globals.count; ++i)
		if (strcmp(ctx->globals.items[i].name, name) == 0)
			return &ctx->globals.items[i];
	return NULL;
}

static struct ld_global *
get_global(struct ld_context *ctx, const char *name)
{
	struct ld_global *globals;
	struct ld_global *global = find_global(ctx, name);
	if (global)
		return global;
	if (ctx->globals.count == ctx->globals.capacity) {
		size_t capacity = ctx->globals.capacity ? ctx->globals.capacity * 2 : 64;
		globals = (struct ld_global *)ld_realloc(
		    ctx->globals.items, capacity * sizeof(*globals));
		if (!globals)
			return NULL;
		ctx->globals.items = globals;
		ctx->globals.capacity = capacity;
	}
	global = &ctx->globals.items[ctx->globals.count++];
	memset(global, 0, sizeof(*global));
	global->name = ld_strdup(name);
	if (!global->name) {
		--ctx->globals.count;
		return NULL;
	}
	return global;
}

static int
prepare_object_symbol(struct ld_context *ctx, struct ld_object *object)
{
	struct mt_elf64_section section;
	uint16_t i;
	uint16_t sec_count = object_section_count(object);
	object->has_symtab = 0;
	for (i = 0; i < sec_count; ++i) {
		if (object_get_section(object, i, &section) != 0)
			return ld_errorf(ctx, "invalid symbol section", object->name);
		if (section.type != MT_SHT_SYMTAB)
			continue;
		if (section.link >= sec_count)
			return ld_errorf(ctx, "invalid symbol table", object->name);
		if (object->elf_class == 1) {
			struct mt_elf32_section strtab32;
			if (mt_elf32_get_section(object->data, object->size,
			                         &object->view.v32,
			                         (uint16_t)section.link,
			                         &strtab32) != MT_ELF_OK ||
			    strtab32.type != MT_SHT_STRTAB ||
			    section.entry_size < MT_ELF32_SYM_SIZE ||
			    section.size % section.entry_size != 0)
				return ld_errorf(ctx, "invalid symbol table", object->name);
			object->strtab.v32 = strtab32;
			/* Convert the 64-bit section fields back to 32-bit for storage */
			{
				struct mt_elf32_section sym32;
				sym32.name = section.name;
				sym32.type = section.type;
				sym32.flags = (uint32_t)section.flags;
				sym32.address = (uint32_t)section.address;
				sym32.offset = (uint32_t)section.offset;
				sym32.size = (uint32_t)section.size;
				sym32.link = section.link;
				sym32.info = section.info;
				sym32.alignment = (uint32_t)section.alignment;
				sym32.entry_size = (uint32_t)section.entry_size;
				object->symtab.v32 = sym32;
			}
		} else {
			if (mt_elf64_get_section(object->data, object->size,
			                         &object->view.v64,
			                         (uint16_t)section.link,
			                         &object->strtab.v64) != MT_ELF_OK ||
			    object->strtab.v64.type != MT_SHT_STRTAB ||
			    section.entry_size < MT_ELF64_SYM_SIZE ||
			    section.size % section.entry_size != 0)
				return ld_errorf(ctx, "invalid symbol table", object->name);
			object->symtab.v64 = section;
		}
		object->symtab_index = i;
		object->has_symtab = 1;
		break;
	}
	return 0;
}

static int
register_global_symbol(struct ld_context *ctx, struct ld_object *object,
                       uint64_t index, const struct mt_elf64_symbol *symbol,
                       const char *name)
{
	struct ld_global *global;
	unsigned binding = symbol->info >> LD_STB_SHIFT;
	int defined = symbol->section != MT_SHN_UNDEF &&
	              symbol->section != LD_SHN_COMMON;
	int common = symbol->section == LD_SHN_COMMON;

	if (!name || !*name || binding == LD_STB_LOCAL)
		return 0;
	global = get_global(ctx, name);
	if (!global)
		return ld_error(ctx, "out of memory");
	if (defined) {
		if (symbol->section >= object_section_count(object) ||
		    object->maps[symbol->section].group < 0)
			return ld_errorf(ctx, "symbol points to discarded section", name);
		if (global->defined && !global->weak && binding != LD_STB_WEAK)
			return ld_errorf(ctx, "multiple definition", name);
		if (!global->defined || (global->weak && binding != LD_STB_WEAK)) {
			global->object = object;
			global->symbol_index = index;
			global->group = object->maps[symbol->section].group;
			global->offset = object->maps[symbol->section].offset + symbol->value;
			global->size = symbol->size;
			global->align = 1;
			global->defined = 1;
			global->weak = binding == LD_STB_WEAK;
			global->common = 0;
		}
	} else if (common) {
		if (!global->defined) {
			global->common = 1;
			if (global->size < symbol->size)
				global->size = symbol->size;
			if (global->align < symbol->value)
				global->align = symbol->value;
		}
	} else if (!global->defined) {
		global->weak = binding == LD_STB_WEAK;
	}
	return 0;
}

static int
collect_one_object_symbols(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_symbol symbol;
	const char *name;
	uint64_t j, sym_count;
	if (prepare_object_symbol(ctx, object) != 0)
		return -1;
	if (!object->has_symtab)
		return 0;
	/* Copy symtab/strtab to 64-bit local vars for iteration */
	if (object->elf_class == 1) {
		struct mt_elf32_section *s32 = &object->symtab.v32;
		sym_count = s32->size / s32->entry_size;
	} else {
		struct mt_elf64_section *s64 = &object->symtab.v64;
		sym_count = s64->size / s64->entry_size;
	}
	for (j = 0; j < sym_count; ++j) {
		if (object_get_symbol(object, j, &symbol) != 0)
			return ld_errorf(ctx, "invalid symbol", object->name);
		if (symbol.name == 0 ||
		    object_get_strtab_string(object, symbol.name, &name) != 0)
			continue;
		if (register_global_symbol(ctx, object, j, &symbol, name) != 0)
			return -1;
	}
	return 0;
}

static int
collect_symbols(struct ld_context *ctx)
{
	size_t i;
	for (i = 0; i < ctx->objects.count; ++i)
		if (collect_one_object_symbols(ctx, i) != 0)
			return -1;
	return 0;
}

/* Apply --defsym: create or override a symbol at a fixed absolute address.
 * Format: SYM=VAL (VAL may use a 0x prefix for hexadecimal).
 * The new global is marked absolute so symbol_value() returns its raw
 * address without consulting any output section. */
static int
apply_defsym(struct ld_context *ctx, const char *defsym)
{
	char symname[256];
	const char *eq, *valstr;
	size_t namelen;
	uint64_t val;
	struct ld_global *global;

	if (!defsym || !*defsym)
		return 0;
	eq = strchr(defsym, '=');
	if (!eq) {
		ld_errorf(ctx, "invalid --defsym (expect SYM=VAL)", defsym);
		return -1;
	}
	namelen = (size_t)(eq - defsym);
	if (namelen == 0 || namelen >= sizeof(symname)) {
		ld_errorf(ctx, "invalid --defsym symbol name", defsym);
		return -1;
	}
	memcpy(symname, defsym, namelen);
	symname[namelen] = '\0';

	valstr = eq + 1;
	if (valstr[0] == '0' && (valstr[1] == 'x' || valstr[1] == 'X')) {
		if (sscanf(valstr + 2, "%llx", (unsigned long long *)&val) != 1) {
			ld_errorf(ctx, "invalid --defsym hexadecimal value", defsym);
			return -1;
		}
	} else {
		val = (uint64_t)atol(valstr);
	}

	global = get_global(ctx, symname);
	if (!global)
		return ld_error(ctx, "out of memory");
	global->defined = 1;
	global->absolute = 1;
	global->weak = 0;
	global->common = 0;
	global->group = -1;
	global->offset = val;
	return 0;
}

/* Apply --wrap: redirect references to SYM to __wrap_SYM, and make
 * __real_SYM point at the original SYM definition (if any).
 *
 * The implementation reuses struct ld_global's alias field:
 *   SYM->alias = __wrap_SYM
 * so that any lookup of SYM via symbol_value() (and related resolvers)
 * follows the alias to __wrap_SYM.
 *
 * If SYM was defined, __real_SYM is set up as a copy of SYM's definition,
 * giving the user access to the original function/data. */
static int
apply_wrap(struct ld_context *ctx, const char *sym)
{
	char wrap_name[256];
	char real_name[256];
	struct ld_global *g, *gw, *gr;

	if (!sym || !*sym)
		return 0;

	snprintf(wrap_name, sizeof wrap_name, "__wrap_%s", sym);
	snprintf(real_name, sizeof real_name, "__real_%s", sym);

	g  = get_global(ctx, sym);
	gw = get_global(ctx, wrap_name);
	gr = get_global(ctx, real_name);
	if (!g || !gw || !gr)
		return ld_error(ctx, "out of memory");

	/* If SYM has a definition, copy it to __real_SYM so the wrapped
	 * function can call __real_foo to access the original. */
	if (g->defined) {
		gr->defined   = 1;
		gr->absolute  = g->absolute;
		gr->weak      = g->weak;
		gr->common    = g->common;
		gr->group     = g->group;
		gr->offset    = g->offset;
		gr->size      = g->size;
		gr->align     = g->align;
		gr->object    = g->object;
		gr->symbol_index = g->symbol_index;
	}

	/* Redirect SYM to __wrap_SYM: any UNDEF reference to SYM will now
	 * resolve to __wrap_SYM (via the alias field in symbol_value()). */
	g->alias = gw;
	return 0;
}

struct archive_extract_context {
	struct ld_context *ctx;
	const char *archive;
	int added;
};

static int
archive_member_needed(struct ld_context *ctx, const unsigned char *data,
                      size_t size)
{
	return archive_member_needed_impl(ctx, data, size, ctx->target->elf_class);
}

static int
extract_archive_member(const struct mt_ar_member *member,
                       const unsigned char *data, void *context)
{
	struct archive_extract_context *extract =
	    (struct archive_extract_context *)context;
	struct ld_context *ctx = extract->ctx;
	char display[512];
	size_t index;
	int needed = ctx->whole_archive ? 1 :
	             archive_member_needed(ctx, data, (size_t)member->size);
	if (needed < 0)
		return ld_errorf(ctx, "invalid archive member", member->name);
	if (!needed)
		return 0;
	snprintf(display, sizeof(display), "%s(%s)", extract->archive, member->name);
	index = ctx->objects.count;

	/* With --whole-archive, skip already-extracted members to avoid
	 * infinite re-extraction in the do-while loop. */
	if (ctx->whole_archive) {
		size_t oi;
		for (oi = 0; oi < ctx->objects.count; oi++) {
			if (strcmp(ctx->objects.items[oi].name, display) == 0)
				return 0; /* already extracted */
		}
	}

	if (append_object(ctx, display, data, (size_t)member->size) != 0)
		return -1;
	if (collect_one_object_sections(ctx, index) != 0)
		return -1;
	if (collect_one_object_symbols(ctx, index) != 0)
		return -1;
	extract->added = 1;
	return 0;
}

static int
extract_archives(struct ld_context *ctx)
{
	int changed;
	size_t i;
	do {
		changed = 0;
		for (i = 0; i < ctx->archives.count; ++i) {
			struct archive_extract_context extract = {
				.ctx = ctx, .archive = ctx->archives.paths[i], .added = 0
			};
			/* Use in-memory archive data if available (VFS .msys) */
			if (i < ctx->archive_mem_count &&
			    ctx->archive_mem_data[i] != NULL) {
				if (mt_ar_foreach_mem(ctx->archive_mem_data[i],
				                      ctx->archive_mem_size[i],
				                      extract_archive_member,
				                      &extract) != 0)
					return ld_errorf(ctx, "cannot extract archive",
					                 ctx->archives.paths[i]);
			} else {
				if (mt_ar_foreach(ctx->archives.paths[i],
				                  extract_archive_member,
				                  &extract) != 0)
					return ld_errorf(ctx, "cannot extract archive",
					                 ctx->archives.paths[i]);
			}
			if (extract.added)
				changed = 1;
		}
	} while (changed);
	return 0;
}

static int
allocate_common(struct ld_context *ctx)
{
	struct ld_group *bss;
	size_t i;
	uint64_t offset;
	if (find_group(ctx, ".bss") < 0) {
		if (get_group(ctx, ".bss", MT_SHT_NOBITS,
		              LD_SHF_ALLOC | LD_SHF_WRITE, 8) < 0)
			return ld_error(ctx, "out of memory");
	}
	bss = &ctx->groups[find_group(ctx, ".bss")];
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *global = &ctx->globals.items[i];
		if (!global->common || global->defined)
			continue;
		if (append_group_data(ctx, bss, NULL, (size_t)global->size,
		                      global->align ? global->align : 1,
		                      &offset) != 0)
			return -1;
		global->group = find_group(ctx, ".bss");
		global->offset = offset;
		global->defined = 1;
	}
	return 0;
}

static int
get_symbol_by_index(struct ld_context *ctx, struct ld_object *object,
                    uint64_t index, struct mt_elf64_symbol *symbol,
                    const char **name)
{
	if (!object->has_symtab ||
	    object_get_symbol(object, index, symbol) != 0)
		return ld_errorf(ctx, "relocation symbol is invalid", object->name);
	if (symbol->name == 0) {
		*name = "";
		return 0;
	}
	if (object_get_strtab_string(object, symbol->name, name) != 0)
		return ld_errorf(ctx, "relocation string is invalid", object->name);
	return 0;
}

static int
symbol_value(struct ld_context *ctx, struct ld_object *object,
             uint64_t symbol_index, uint64_t *value, const char **name_out)
{
	struct mt_elf64_symbol symbol;
	const char *name;
	struct ld_global *global;
	struct ld_section_map *map;
	if (get_symbol_by_index(ctx, object, symbol_index, &symbol, &name) != 0)
		return -1;
	if (name_out)
		*name_out = name;
	if (symbol.section == MT_SHN_UNDEF) {
		global = find_global(ctx, name);
		if (!global)
			return ld_errorf(ctx, "undefined symbol", name);
		/* Follow alias chain (from --wrap) */
		while (global->alias)
			global = global->alias;
		if (!global->defined)
			return ld_errorf(ctx, "undefined symbol", name);
		if (global->absolute)
			*value = global->offset + symbol.value;
		else
			*value = ctx->groups[global->group].address +
			         global->offset + symbol.value;
		return 0;
	}
	if (symbol.section == LD_SHN_COMMON) {
		global = find_global(ctx, name);
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined common symbol", name);
		*value = ctx->groups[global->group].address + global->offset;
		return 0;
	}
	if (symbol.section >= object_section_count(object) ||
	    object->maps[symbol.section].group < 0)
		return ld_errorf(ctx, "symbol section was discarded", name);
	map = &object->maps[symbol.section];
	*value = ctx->groups[map->group].address + map->offset + symbol.value;
	return 0;
}

static int
got_index(struct ld_context *ctx, const char *name, size_t *index)
{
	size_t i;
	for (i = 0; i < ctx->got.count; ++i)
		if (strcmp(ctx->got.items[i].name, name) == 0) {
			if (index)
				*index = i;
			return 0;
		}
	return -1;
}

static int
add_got_entry(struct ld_context *ctx, const char *name)
{
	struct ld_got_entry *items;
	if (!name || !*name)
		return ld_error(ctx, "GOT relocation has no symbol");
	if (find_group(ctx, ".got") < 0 &&
	    get_group(ctx, ".got", MT_SHT_PROGBITS,
	              LD_SHF_ALLOC | LD_SHF_WRITE, 8) < 0)
		return ld_error(ctx, "out of memory");
	if (got_index(ctx, name, NULL) == 0)
		return 0;
	if (ctx->got.count == ctx->got.capacity) {
		size_t capacity = ctx->got.capacity ? ctx->got.capacity * 2 : 16;
		items = (struct ld_got_entry *)ld_realloc(ctx->got.items,
		                                          capacity * sizeof(*items));
		if (!items)
			return ld_error(ctx, "out of memory");
		ctx->got.items = items;
		ctx->got.capacity = capacity;
	}
	ctx->got.items[ctx->got.count].name = ld_strdup(name);
	if (!ctx->got.items[ctx->got.count].name)
		return ld_error(ctx, "out of memory");
	ctx->got.items[ctx->got.count].offset = ctx->got.count * 8;
	++ctx->got.count;
	return 0;
}

static int
collect_got_relocations(struct ld_context *ctx)
{
	uint16_t i;
	size_t j;
	struct mt_elf64_section section;
	struct mt_elf64_symbol symbol;
	const char *name;
	uint64_t n;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, (uint16_t)j, &section) != 0)
				return -1;
			if (section.type != MT_SHT_RELA)
				continue;
			if (object->elf_class == 1) {
				/* ELF32 RELA: 12 bytes per entry */
				uint32_t info32;
				if (section.entry_size < 12 ||
				    section.size % section.entry_size != 0)
					continue;
				for (n = 0; n < section.size / section.entry_size; ++n) {
					const unsigned char *p = object->data + section.offset + n * 12;
					info32 = read32(p + 4);
					if ((unsigned)info32 == LD_R_X86_64_GOTPCREL ||
					    (info32 & 0xff) == 75 || (info32 & 0xff) == 76) {
						uint32_t sym_idx = info32 >> 8;
						if (get_symbol_by_index(ctx, object, sym_idx, &symbol,
						                        &name) != 0 ||
						    add_got_entry(ctx, name) != 0)
							return -1;
					}
				}
			} else {
				uint64_t info64;
				if (section.entry_size < 24 ||
				    section.size % section.entry_size != 0)
					continue;
				for (n = 0; n < section.size / section.entry_size; ++n) {
					const unsigned char *p = object->data + section.offset + n * section.entry_size;
					info64 = read64(p + 8);
					if ((unsigned)info64 != LD_R_X86_64_GOTPCREL) {
						unsigned rel_type = (unsigned)(info64 & 0xffffffff);
						if (rel_type == 75 || rel_type == 76)
							goto collect_got64;
						continue;
					}
					collect_got64:;
					if (get_symbol_by_index(ctx, object, info64 >> 32, &symbol,
					                        &name) != 0 ||
					    add_got_entry(ctx, name) != 0)
						return -1;
				}
			}
		}
	}
	if (ctx->got.count != 0) {
		struct ld_group *got = &ctx->groups[find_group(ctx, ".got")];
		if (append_group_data(ctx, got, NULL, ctx->got.count * 8, 8,
		                      &n) != 0)
			return -1;
	}
	return 0;
}

/* Apply linker script: override section ranks for placement order.
 * Script format: one "section_name = rank" per line.
 * Lower rank = earlier placement (rank 0 = first).
 * Lines starting with '#' are comments. */
static int
apply_link_script(struct ld_context *ctx, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return ld_errorf(ctx, "cannot open link script", path);

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '#' || *p == '\n' || *p == '\0') continue;

		/* Parse: section_name = rank_number */
		char name[128];
		int n = 0;
		while (*p && *p != '=' && *p != ' ' && *p != '\t' && n < 126)
			name[n++] = *p++;
		name[n] = '\0';
		while (*p && (*p == ' ' || *p == '\t' || *p == '=')) p++;
		if (*p) {
			int rank = atoi(p);
			int g = find_group(ctx, name);
			if (g >= 0)
				ctx->groups[g].rank = rank;
		}
	}
	fclose(f);
	return 0;
}

static int
layout_output(struct ld_context *ctx)
{
	uint64_t offset = LD_PAGE;
	/* For shared libraries (ET_DYN) the base load address is 0; the
	 * dynamic loader chooses the final address at load time. */
	uint64_t base = ctx->shared ? 0 : LD_BASE;
	int have_rw = 0;
	int rank;
	size_t i;

	ctx->tls_tdata_group = find_group(ctx, ".tdata");
	ctx->tls_tbss_group = find_group(ctx, ".tbss");
	ctx->tls_tdata_size = 0;
	ctx->tls_size = 0;
	ctx->tls_align = 1;

	/* Calculate TLS block size (variant II: tdata then tbss). */
	if (ctx->tls_tdata_group >= 0 || ctx->tls_tbss_group >= 0) {
		uint64_t tdata = 0, tbss = 0, talign = 1;
		if (ctx->tls_tdata_group >= 0) {
			tdata = ctx->groups[ctx->tls_tdata_group].size;
			talign = ctx->groups[ctx->tls_tdata_group].align;
		}
		if (ctx->tls_tbss_group >= 0) {
			tbss = ctx->groups[ctx->tls_tbss_group].size;
			if (ctx->groups[ctx->tls_tbss_group].align > talign)
				talign = ctx->groups[ctx->tls_tbss_group].align;
		}
		ctx->tls_tdata_size = tdata;
		ctx->tls_align = talign;
		/* variant II: tbss starts after aligned tdata */
		ctx->tls_size = align_up(align_up(tdata, talign) + tbss, talign);
	}

	/* Pass 1: PROGBITS sections (skip TLS .tdata) */
	for (rank = 0; rank <= 5; ++rank) {
		if (rank >= 2 && !have_rw) {
			offset = align_up(offset, LD_PAGE);
			have_rw = 1;
		}
		for (i = 0; i < ctx->group_count; ++i) {
			struct ld_group *group = &ctx->groups[i];
			if (group->rank != rank || group->type == MT_SHT_NOBITS)
				continue;
			if (is_tls_group(ctx, (int)i))
				continue;
			offset = align_up(offset, group->align ? group->align : 1);
			group->file_offset = offset;
			group->address = base + offset;
			offset += group->size;
		}
	}

	/* Pass 2: NOBITS sections (skip TLS .tbss) - always after PROGBITS */
	for (rank = 2; rank <= 5; ++rank) {
		for (i = 0; i < ctx->group_count; ++i) {
			struct ld_group *group = &ctx->groups[i];
			if (group->rank != rank || group->type != MT_SHT_NOBITS)
				continue;
			if (is_tls_group(ctx, (int)i))
				continue;
			offset = align_up(offset, group->align ? group->align : 1);
			group->file_offset = offset;
			group->address = base + offset;
			/* NOBITS: does not advance file offset */
		}
	}

	/* Advance offset past non-TLS sections so TLS does not overlap .bss */
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *g = &ctx->groups[i];
		if (is_tls_group(ctx, (int)i)) continue;
		uint64_t e = g->file_offset + g->size;
		if (e > offset) offset = e;
	}

	/* TLS sections: lay out .tdata (needs file space) then .tbss (NOBITS). */
	if (ctx->tls_tdata_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tdata_group];
		offset = align_up(offset, g->align ? g->align : 1);
		g->file_offset = offset;
		g->address = ctx->shared ? 0 : LD_BASE + offset;
		offset += g->size;
	}
	if (ctx->tls_tbss_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tbss_group];
		if (ctx->tls_tdata_group >= 0) {
			struct ld_group *td = &ctx->groups[ctx->tls_tdata_group];
			g->file_offset = td->file_offset + td->size;
			g->address = td->address + td->size;
		} else {
			/* No .tdata: allocate beyond non-TLS sections */
			g->file_offset = offset;
			g->address = base + offset;
			offset += g->size;
			}
		}
		return 0;
	}


static int
symbol_tls_offset(struct ld_context *ctx, struct ld_object *object,
                  uint64_t symbol_index, uint64_t *tls_offset)
{
	struct mt_elf64_symbol symbol;
	const char *name;
	struct ld_global *global;
	struct ld_section_map *map;
	if (get_symbol_by_index(ctx, object, symbol_index, &symbol, &name) != 0)
		return -1;
	if (symbol.section == MT_SHN_UNDEF) {
		global = find_global(ctx, name);
		if (!global)
			return ld_errorf(ctx, "undefined symbol", name);
		/* Follow alias chain (from --wrap) */
		while (global->alias)
			global = global->alias;
		if (!global->defined)
			return ld_errorf(ctx, "undefined symbol", name);
		if (global->group == ctx->tls_tdata_group)
			*tls_offset = global->offset + symbol.value;
		else if (global->group == ctx->tls_tbss_group)
			*tls_offset = ctx->tls_tdata_size + global->offset + symbol.value;
		else
			return ld_errorf(ctx, "TPOFF32 relocation against non-TLS symbol", name);
		return 0;
	}
	if (symbol.section >= object_section_count(object) ||
	    object->maps[symbol.section].group < 0)
		return ld_errorf(ctx, "symbol section was discarded", name);
	map = &object->maps[symbol.section];
	if (map->group == ctx->tls_tdata_group)
		*tls_offset = map->offset + symbol.value;
	else if (map->group == ctx->tls_tbss_group)
		*tls_offset = ctx->tls_tdata_size + map->offset + symbol.value;
	else
		return ld_errorf(ctx, "TPOFF32 relocation against non-TLS symbol", name);
	return 0;
}

static int
write_relocation(struct ld_context *ctx, struct ld_object *object,
                 const struct mt_elf64_section *reloc_section,
                 struct ld_group *target, uint64_t reloc_index)
{
	const unsigned char *p = object->data + reloc_section->offset +
	                         reloc_index * reloc_section->entry_size;
	uint64_t offset, info, symbol_index;
	int64_t addend;
	int type;
	if (object->elf_class == 1) {
		/* ELF32 RELA: 12-byte entry */
		offset = read32(p + 0);
		info = read32(p + 4);
		type = (int)(info & 0xff);
		symbol_index = info >> 8;
		addend = (int32_t)read32(p + 8);
	} else {
		offset = read64(p + 0);
		info = read64(p + 8);
		type = (int)(info & 0xffffffffu);
		symbol_index = info >> 32;
		addend = (int64_t)read64(p + 16);
	}
	uint64_t resolved_value;
	const char *name;
	uint64_t place;
	uint64_t value;
	size_t got;
	struct ld_group *got_group;
	uint64_t target_offset;
	unsigned width;

	/* Skip relocations targeting GC'd (--gc-sections) sections */
	if (target->size == 0) return 0;
	if (offset > target->size || target->size - offset <
	    (type == LD_R_X86_64_64 ? 8 : 4))
		return ld_error(ctx, "relocation offset is outside output section");
	if (symbol_value(ctx, object, symbol_index, &resolved_value, &name) != 0)
		return -1;
	target_offset = (uint64_t)object->maps[reloc_section->info].offset + offset;
	place = target->address + target_offset;

	/* Per-architecture relocation dispatch.
	 *
	 * WARNING: Architecture-specific relocation type numbers may overlap
	 * with x86_64's LD_R_X86_64_* constants (e.g. R_LARCH_64 == 2 ==
	 * LD_R_X86_64_PC32).  The per-arch check MUST come before the x86_64
	 * type-number comparisons so that the correct formula is used. */
	if (strcmp(ctx->target->name, "loongarch64") == 0) {
		uint64_t la64_got_addr;
		size_t la64_got_idx;

		/* TLS LE relocations need TP-relative offset, not full VA */
		if (type == 83 || type == 84 || type == 85 || type == 86) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off;
		}

		/* GOT-based relocations: route via GOT entry (fill_got handles the data) */
		if (type == 75 || type == 76) { /* R_LARCH_GOT_PC_HI20 / GOT_PC_LO12 */
			if (got_index(ctx, name, &la64_got_idx) != 0)
				return ld_errorf(ctx, "missing GOT entry", name);
			la64_got_addr = ctx->groups[ctx->got.group].address +
			                ctx->got.items[la64_got_idx].offset;
			if (la64_apply_reloc(type, target->data + target_offset,
			                     la64_got_addr, addend, place) == 0)
				return 0;
		} else if (la64_apply_reloc(type, target->data + target_offset,
		                            resolved_value, addend, place) == 0) {
			return 0;
		}
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "aarch64") == 0) {
		if (mt_apply_aarch64_reloc(type, target->data + target_offset,
		                           resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "arm") == 0) {
		if (mt_apply_arm_reloc(type, target->data + target_offset,
		                       resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "riscv64") == 0) {
		/* TLS LE relocations need TP-relative offset, not full VA */
		if (type == 39 || type == 40 || type == 41) {
			uint64_t tls_off;
			if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
				return ld_errorf(ctx, "unsupported TLS relocation", name);
			resolved_value = tls_off;
		}
		if (riscv64_apply_reloc(type, target->data + target_offset,
		                         resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (strcmp(ctx->target->name, "i386") == 0) {
		if (i386_apply_reloc(type, target->data + target_offset,
		                     resolved_value, addend, place) == 0)
			return 0;
		return ld_errorf(ctx, "unsupported relocation type", name);
	}

	/* x86_64-specific type dispatch.  These type numbers are ONLY valid
	 * for x86_64 — arch-specific dispatch above handles all others. */
	if (strcmp(ctx->target->name, "x86_64") != 0)
		return ld_errorf(ctx, "unsupported relocation type", name);
	if (type == LD_R_X86_64_GOTPCREL) {
		if (got_index(ctx, name, &got) != 0)
			return ld_errorf(ctx, "missing GOT entry", name);
		got_group = &ctx->groups[ctx->got.group];
		value = got_group->address + ctx->got.items[got].offset + addend - place;
		width = 4;
	} else if (type == LD_R_X86_64_PC32 || type == LD_R_X86_64_PLT32) {
		value = resolved_value + addend - place;
		width = 4;
	} else if (type == LD_R_X86_64_TPOFF32) {
		/* x86_64 variant II TLS: %fs points at the end of the static
		 * TLS block. tpoff = symbol_offset_in_tls_block - tls_size. */
		uint64_t tls_off;
		if (symbol_tls_offset(ctx, object, symbol_index, &tls_off) != 0)
			return -1;
		value = (uint64_t)((int64_t)tls_off - (int64_t)ctx->tls_size) + addend;
		width = 4;
	} else if (type == LD_R_X86_64_64) {
		value = resolved_value + addend;
		width = 8;
	} else if (type == LD_R_X86_64_32 || type == LD_R_X86_64_32S) {
		value = resolved_value + addend;
		width = 4;
	} else {
		return ld_errorf(ctx, "unsupported relocation type", name);
	}
	if (target->type == MT_SHT_NOBITS || target_offset > target->size ||
	    width > target->size - target_offset)
		return ld_error(ctx, "relocation targets non-file section");
	if (width == 8)
		write64(target->data + target_offset, value);
	else
		write32(target->data + target_offset, (uint32_t)value);
	return 0;
}

static int
apply_relocations(struct ld_context *ctx)
{
	size_t i;
	uint16_t j;
	uint64_t n;
	struct mt_elf64_section section;
	int group;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, j, &section) != 0)
				return ld_errorf(ctx, "invalid relocation section", object->name);
			if (section.type != MT_SHT_RELA)
				continue;
			if (section.info >= object_section_count(object) ||
			    object->maps[section.info].group < 0)
				return ld_error(ctx, "relocation target was discarded");
			group = object->maps[section.info].group;
			/* Skip relocations targeting GC'd sections */
			if (ctx->groups[group].size == 0) continue;
			for (n = 0; n < section.size / section.entry_size; ++n)
				if (write_relocation(ctx, object, &section,
				                    &ctx->groups[group], n) != 0)
					return -1;
		}
	}
	return 0;
}

static int
fill_got(struct ld_context *ctx)
{
	size_t i;
	struct ld_group *got;
	struct ld_global *global;
	uint64_t value;
	if (ctx->got.count == 0)
		return 0;
	got = &ctx->groups[ctx->got.group];
	for (i = 0; i < ctx->got.count; ++i) {
		global = find_global(ctx, ctx->got.items[i].name);
		if (global && global->alias)
			global = global->alias;
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined GOT symbol", ctx->got.items[i].name);
		value = ctx->groups[global->group].address + global->offset;
		write64(got->data + ctx->got.items[i].offset, value);
	}
	return 0;
}

/* ---- .rela.dyn dynamic relocation section ---- */

/* Append one RELA entry to the .rela.dyn section.
 * ELF64 RELA: 24 bytes (r_offset:8, r_info:8, r_addend:8). */
static int
rela_dyn_add(struct ld_context *ctx, uint64_t offset, uint64_t info, int64_t addend)
{
	int rg = find_group(ctx, ".rela.dyn");
	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t entry_size = elf64 ? 24 : 12;
	unsigned char entry[24];
	if (rg < 0) return -1;
	if (elf64) {
		write64(entry, offset);
		write64(entry + 8, info);
		write64(entry + 16, (uint64_t)addend);
	} else {
		write32(entry, (uint32_t)offset);
		write32(entry + 4, (uint32_t)info);
		write32(entry + 8, (uint32_t)addend);
	}
	uint64_t n;
	return append_group_data(ctx, &ctx->groups[rg], entry, entry_size, 8, &n);
}

/* Build the .rela.dyn section from GOT entries for shared library output.
 * Each GOT entry gets an R_X86_64_RELATIVE dynamic relocation so that
 * ld.so can adjust the GOT value at load time. */
static int
build_rela_dyn(struct ld_context *ctx)
{
	size_t i;
	if (!ctx->shared || ctx->got.count == 0)
		return 0;
	/* .rela.dyn was pre-created in build_dynamic_tables */
	int rg = find_group(ctx, ".rela.dyn");
	if (rg < 0) return 0; /* no GOT entries, nothing to relocate */
	uint64_t got_addr = ctx->groups[ctx->got.group].address;
	for (i = 0; i < ctx->got.count; ++i) {
		uint64_t got_entry = got_addr + ctx->got.items[i].offset;
		struct ld_global *g = find_global(ctx, ctx->got.items[i].name);
		if (!g) continue;
		uint64_t sym_value = ctx->groups[g->group].address + g->offset;
		/* R_X86_64_RELATIVE: *(base + got_entry) = base + sym_value */
		uint64_t info = ((uint64_t)MT_R_X86_64_RELATIVE);
		if (ctx->target->elf_class == MT_ELFCLASS32)
			info = (uint64_t)MT_R_X86_64_RELATIVE;
		else
			info = (0ULL << 32) | MT_R_X86_64_RELATIVE;
		if (rela_dyn_add(ctx, got_entry, info, (int64_t)sym_value) != 0)
			return -1;
	}
	return 0;
}

/* ---- Build ID: FNV-1a 64-bit hash over output data ---- */

/* FNV-1a 64-bit hash, offset basis and prime from the FNV spec.
 * Reference: http://www.isthe.com/chongo/tech/comp/fnv/ */
struct ld_output_section {
	const char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t address;
	uint64_t offset;
	uint64_t size;
	uint64_t align;
	uint32_t link;   /* sh_link */
	uint32_t info;   /* sh_info */
	uint64_t entry_size; /* sh_entsize */
};

struct ld_strings {
	unsigned char *data;
	size_t size;
	size_t capacity;
};

static int
strings_init(struct ld_strings *strings)
{
	memset(strings, 0, sizeof(*strings));
	strings->data = (unsigned char *)ld_malloc(1);
	if (!strings->data)
		return -1;
	strings->data[0] = 0;
	strings->size = strings->capacity = 1;
	return 0;
}

static int
strings_add(struct ld_strings *strings, const char *name, uint32_t *offset)
{
	size_t length = strlen(name);
	size_t needed;
	unsigned char *data;
	size_t capacity;
	if (length > UINT32_MAX || strings->size > SIZE_MAX - length - 1)
		return -1;
	needed = strings->size + length + 1;
	if (needed > strings->capacity) {
		capacity = strings->capacity * 2;
		while (capacity < needed)
			capacity *= 2;
		data = (unsigned char *)ld_realloc(strings->data, capacity);
		if (!data)
			return -1;
		strings->data = data;
		strings->capacity = capacity;
	}
	*offset = (uint32_t)strings->size;
	memcpy(strings->data + strings->size, name, length + 1);
	strings->size = needed;
	return 0;
}

#define LD_PT_TLS 7

static int
write_program_header_type(FILE *file, uint32_t type, uint32_t flags,
                          uint64_t offset, uint64_t address,
                          uint64_t file_size, uint64_t memory_size,
                          uint64_t align)
{
	unsigned char p[56] = {0};
	write32(p + 0, type);
	write32(p + 4, flags);
	write64(p + 8, offset);
	write64(p + 16, address);
	write64(p + 24, address);
	write64(p + 32, file_size);
	write64(p + 40, memory_size);
	write64(p + 48, align);
	return fwrite(p, 1, sizeof(p), file) == sizeof(p) ? 0 : -1;
}

static int
write_program_header(FILE *file, uint32_t flags, uint64_t offset,
                     uint64_t address, uint64_t file_size, uint64_t memory_size)
{
	return write_program_header_type(file, LD_PT_LOAD, flags, offset, address,
	                                file_size, memory_size, LD_PAGE);
}

/* ---- .eh_frame_hdr support (--eh-frame-hdr) ----

 * Parse .eh_frame, extract FDE PC ranges.
 * pc_abs_buf (if non-NULL) receives nfde pairs of {pc_start(8), fde_offset(8)}.
 * Returns number of FDEs found. */

/* DWARF EH pointer encoding constants */
#define DW_EH_PE_absptr  0x00
#define DW_EH_PE_uleb128 0x01
#define DW_EH_PE_udata2  0x02
#define DW_EH_PE_udata4  0x03
#define DW_EH_PE_udata8  0x04
#define DW_EH_PE_sleb128 0x09
#define DW_EH_PE_sdata2  0x0a
#define DW_EH_PE_sdata4  0x0b
#define DW_EH_PE_sdata8  0x0c
#define DW_EH_PE_pcrel   0x10
#define DW_EH_PE_textrel 0x20
#define DW_EH_PE_datarel 0x30
#define DW_EH_PE_funcrel 0x40
#define DW_EH_PE_aligned 0x50

/* Return the byte width of an EH pointer encoding (0 for ULEB/SLEB). */
static int
eh_pe_width(int encoding, int elf64)
{
	switch (encoding & 0x0f) {
	case DW_EH_PE_absptr:  return elf64 ? 8 : 4;
	case DW_EH_PE_udata2:
	case DW_EH_PE_sdata2:  return 2;
	case DW_EH_PE_udata4:
	case DW_EH_PE_sdata4:  return 4;
	case DW_EH_PE_udata8:
	case DW_EH_PE_sdata8:  return 8;
	default:               return 0; /* ULEB128 / SLEB128 / unknown */
	}
}

/* Read an encoded value at *pp, advancing *pp past it.  pe_base is the
 * section-relative base for DW_EH_PE_datarel.  Returns the absolute value. */
static uint64_t
eh_read_encoded(const unsigned char **pp, int encoding,
                uint64_t pe_base, uint64_t cur_addr, int elf64)
{
	const unsigned char *p = *pp;
	int w = eh_pe_width(encoding, elf64);
	uint64_t val;

	if (w == 0) {
		/* ULEB128 / SLEB128 — not commonly used for EH frame, skip */
		*pp = p;
		return 0;
	}

	switch (encoding & 0x0f) {
	case DW_EH_PE_absptr:
		val = elf64 ? read64(p) : read32(p);
		break;
	case DW_EH_PE_udata2: val = read16(p); break;
	case DW_EH_PE_sdata2: val = (uint64_t)(int64_t)(int16_t)read16(p); break;
	case DW_EH_PE_udata4: val = read32(p); break;
	case DW_EH_PE_sdata4: {
		uint32_t raw = read32(p);
		int32_t s32 = (int32_t)raw;
		val = (uint64_t)(int64_t)s32;
		break;
	}
	case DW_EH_PE_udata8: val = read64(p); break;
	case DW_EH_PE_sdata8: val = (uint64_t)(int64_t)read64(p); break;
	default: val = 0; break;
	}
	p += w;

	if (encoding & DW_EH_PE_pcrel) {
		val = cur_addr + val;
	}
	if ((encoding & 0x70) == DW_EH_PE_datarel)
		val = pe_base + val;

	*pp = p;
	return val;
}

/* Context for building .eh_frame_hdr */
struct eh_frame_fde {
	uint64_t pc_start;
	uint64_t fde_offset; /* offset of FDE within .eh_frame */
	int has_pc;          /* 1 if pc_start was successfully parsed */
};

/* Parse .eh_frame data and collect FDE entries.
 * fdes is a caller-owned buffer (can be NULL for first pass to count).
 * Returns number of FDEs found. */
static size_t
eh_parse_fdes(const unsigned char *data, size_t size,
              struct eh_frame_fde *fdes, size_t max_fdes,
              uint64_t eh_frame_addr, int elf64)
{
	size_t offset = 0;
	size_t nfde = 0;
	int fde_encoding = DW_EH_PE_absptr; /* default: native pointer */
	int addr_encoding = DW_EH_PE_udata4; /* default: 4-byte */

	while (offset + 8 <= size) {
		uint32_t len = read32(data + offset);
		if (len == 0) break; /* end marker */

		uint32_t end = offset + 4 + len;
		if (end > size) break;

		uint32_t cie_id = read32(data + offset + 4);

		if (cie_id == 0) {
			/* CIE record */
			if (len < 8) { offset = end; continue; }
			const char *aug = (const char *)data + offset + 9;
			size_t aug_len = strlen(aug);
			const unsigned char *p = (const unsigned char *)aug + aug_len + 1;

			/* code alignment factor (ULEB128) */
			uint64_t caf = 0; int shift = 0;
			while (*p & 0x80) { caf |= (uint64_t)(*p++ & 0x7f) << shift; shift += 7; }
			caf |= (uint64_t)(*p++ & 0x7f) << shift;

			/* data alignment factor (SLEB128) */
			int64_t daf = 0; shift = 0; int b;
			do { b = *p++; daf |= (int64_t)(b & 0x7f) << shift; shift += 7; }
			while (b & 0x80);
			if (shift > 7 && (b & 0x40))
				daf |= -(1LL << shift);

			/* return address register */
			p++; /* version 1: 1 byte */

			/* Check augmentation for 'R' (FDE encoding) and 'z' (length) */
			fde_encoding = DW_EH_PE_absptr;
			addr_encoding = DW_EH_PE_udata4;
			int has_z = 0;
			for (const char *ap = aug; *ap; ap++) {
				if (*ap == 'z') has_z = 1;
			}
			if (has_z) {
				/* augmentation length (ULEB128) */
				uint64_t augdata_len = 0; shift = 0;
				while (*p & 0x80) {
					augdata_len |= (uint64_t)(*p++ & 0x7f) << shift;
					shift += 7;
				}
				augdata_len |= (uint64_t)(*p++ & 0x7f) << shift;
				const unsigned char *augstart = p;
				for (const char *ap = aug; *ap; ap++) {
					if (*ap == 'R') {
						fde_encoding = *p++;
						/* Derive address_range encoding (plain width, no pcrel) */
						int w = eh_pe_width(fde_encoding, elf64);
						if (w == 8) addr_encoding = DW_EH_PE_udata8;
						else if (w == 4) addr_encoding = DW_EH_PE_udata4;
						else if (w == 2) addr_encoding = DW_EH_PE_udata2;
					} else if (*ap == 'L') {
						p++; /* LSDA encoding */
					} else if (*ap == 'P') {
						p++; /* personality encoding */
						int pw = eh_pe_width(*p, elf64);
						p++; p += pw;
					}
					/* 'z' is handled by has_z above — no per-loop work needed */
				}
				p = augstart + augdata_len;
			}
			offset = end;
			continue;
		}
		/* FDE record */
		const unsigned char *fp = data + offset + 8;
		uint64_t fde_addr = eh_frame_addr + offset + 8;
		uint64_t pc_start = eh_read_encoded(&fp, fde_encoding,
		                                     eh_frame_addr, fde_addr, elf64);
		eh_read_encoded(&fp, addr_encoding,
		                      eh_frame_addr, 0, elf64);

		if (nfde < max_fdes && fdes) {
			fdes[nfde].pc_start = pc_start;
			fdes[nfde].fde_offset = offset;
			fdes[nfde].has_pc = 1;
		}
		nfde++;

		offset = end;
	}

	return nfde;
}

/* Comparison function for qsort: sort FDEs by pc_start. */
static int
eh_fde_cmp(const void *a, const void *b)
{
	const struct eh_frame_fde *fa = (const struct eh_frame_fde *)a;
	const struct eh_frame_fde *fb = (const struct eh_frame_fde *)b;
	if (fa->pc_start < fb->pc_start) return -1;
	if (fa->pc_start > fb->pc_start) return 1;
	return 0;
}

/* Generate .eh_frame_hdr data with addresses already encoded.
 * hdr_addr is the VMA of the output .eh_frame_hdr section. */
static int
build_eh_frame_hdr(struct ld_context *ctx, unsigned char **out_data,
                   size_t *out_size, uint64_t hdr_addr)
{
	int ehg = find_group(ctx, ".eh_frame");
	if (ehg < 0) { *out_data = NULL; *out_size = 0; return 0; }
	struct ld_group *ehg_g = &ctx->groups[ehg];
	if (ehg_g->size == 0 || ehg_g->type == MT_SHT_NOBITS)
		{ *out_data = NULL; *out_size = 0; return 0; }

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t nfde = eh_parse_fdes(ehg_g->data, ehg_g->size, NULL, 0,
	                             ehg_g->address, elf64);
	if (nfde == 0) { *out_data = NULL; *out_size = 0; return 0; }

	struct eh_frame_fde *fdes = (struct eh_frame_fde *)
	    ld_malloc(nfde * sizeof(struct eh_frame_fde));
	if (!fdes) return ld_error(ctx, "out of memory");
	eh_parse_fdes(ehg_g->data, ehg_g->size, fdes, nfde,
	              ehg_g->address, elf64);
	qsort(fdes, nfde, sizeof(struct eh_frame_fde), eh_fde_cmp);

	size_t hdr_size = 12 + nfde * 8;
	unsigned char *hdr = (unsigned char *)ld_malloc(hdr_size ? hdr_size : 1);
	if (!hdr) { free(fdes); return ld_error(ctx, "out of memory"); }
	memset(hdr, 0, hdr_size);

	hdr[0] = 1;                       /* version */
	hdr[1] = 0x1b;                    /* .eh_frame ptr: pcrel | sdata4 */
	hdr[2] = 0x03;                    /* FDE count: udata4 */
	hdr[3] = 0x3b;                    /* table: datarel | sdata4 */

	/* .eh_frame pcrel from (hdr_addr + 4) */
	write32(hdr + 4, (uint32_t)(int32_t)((int64_t)ehg_g->address -
	                                      (int64_t)(hdr_addr + 4)));
	write32(hdr + 8, (uint32_t)nfde);

	/* Fill table: datarel from hdr_addr */
	int64_t base = (int64_t)hdr_addr;
	size_t ti;
	for (ti = 0; ti < nfde; ti++) {
		uint64_t fde_va = ehg_g->address + fdes[ti].fde_offset;
		int64_t loc_delta = (int64_t)fdes[ti].pc_start - base;
		int64_t fde_delta = (int64_t)fde_va - base;
		write32(hdr + 12 + ti * 8, (uint32_t)(int32_t)loc_delta);
		write32(hdr + 12 + ti * 8 + 4, (uint32_t)(int32_t)fde_delta);
	}
	free(fdes);
	*out_data = hdr;
	*out_size = hdr_size;
	return 0;
}

static int
write_executable(struct ld_context *ctx, const char *path,
                 const char *entry, const struct mt_target *target)
{
	struct ld_group *entry_group = NULL;
	struct ld_global *entry_symbol;
	struct ld_strings shstr;
	struct ld_output_section *sections = NULL;
	uint32_t *name_offsets = NULL;
	unsigned char *eh_frame_hdr_data = NULL; /* generated eh_frame_hdr content */
	size_t eh_frame_hdr_size = 0;
	FILE *file = NULL;
	uint64_t entry_address;
	uint64_t rx_end = LD_PAGE;
	uint64_t rw_start = UINT64_MAX;
	uint64_t file_end = LD_PAGE;
	uint64_t memory_end = LD_PAGE;
	uint64_t section_offset;
	uint64_t alloc_file_end;
	uint32_t shstr_index;
	size_t i;
	int output_count;
	int result = -1;
	/* For shared libraries (ET_DYN) the load base is 0; the dynamic
	 * loader relocates the image at run time. */
	uint64_t base_addr = (ctx->shared || ctx->pie) ? 0 : LD_BASE;
	/* Program header count: PT_LOAD + optional PT_TLS + (shared) PT_PHDR/PT_DYNAMIC. */
	int phnum;

	entry_symbol = NULL;
	entry_address = 0;
	if (entry) {
		entry_symbol = find_global(ctx, entry);
		if (!entry_symbol || !entry_symbol->defined)
			return ld_errorf(ctx, "entry symbol not found", entry);
		entry_group = &ctx->groups[entry_symbol->group];
		entry_address = entry_group->address + entry_symbol->offset;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (group->rank < 2)
			rx_end = group->type == MT_SHT_NOBITS ? rx_end :
			          group->file_offset + group->size > rx_end ?
			          group->file_offset + group->size : rx_end;
		else if (rw_start == UINT64_MAX || group->file_offset < rw_start)
			rw_start = group->file_offset;
		if ((group->flags & MT_SHF_ALLOC) &&
		    group->type != MT_SHT_NOBITS &&
		    file_end < group->file_offset + group->size)
			file_end = group->file_offset + group->size;
		if ((group->flags & MT_SHF_ALLOC) &&
		    memory_end < group->file_offset + group->size)
			memory_end = group->file_offset + group->size;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		if ((ctx->groups[i].flags & MT_SHF_ALLOC) &&
		    ctx->groups[i].type == MT_SHT_NOBITS &&
		    memory_end < ctx->groups[i].file_offset + ctx->groups[i].size)
			memory_end = ctx->groups[i].file_offset + ctx->groups[i].size;
	}
	output_count = (int)ctx->group_count + 2; /* null + groups + shstrtab */
	if (ctx->build_id)
		output_count++; /* +1 for .note.gnu.build-id */
	if (ctx->eh_frame_hdr) {
		/* Just check if .eh_frame exists to decide section count.
		 * Actual hdr content is generated later once address is known. */
		int ehg = find_group(ctx, ".eh_frame");
		if (ehg >= 0 && ctx->groups[ehg].size > 0 &&
		    ctx->groups[ehg].type != MT_SHT_NOBITS)
			output_count++; /* +1 for .eh_frame_hdr */
	}
	sections = (struct ld_output_section *)calloc(output_count,
	                                               sizeof(*sections));
	name_offsets = (uint32_t *)calloc(output_count, sizeof(*name_offsets));
	if (!sections || !name_offsets)
		goto out;
	if (strings_init(&shstr) != 0)
		goto out;
	for (i = 0; i < ctx->group_count; ++i) {
		const char *gn = ctx->groups[i].name;
		int is_elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
		uint64_t es = 0; /* sh_entsize */
		if (gn && strcmp(gn, ".dynsym") == 0)
			es = is_elf64 ? MT_ELF64_SYM_SIZE : (uint64_t)MT_ELF32_SYM_SIZE;
		else if (gn && strcmp(gn, ".rela.dyn") == 0)
			es = is_elf64 ? 24 : 12;
		else if (gn && strcmp(gn, ".hash") == 0)
			es = 4;
		sections[i + 1] = (struct ld_output_section){
			.name = gn,
			.type = ctx->groups[i].type,
			.flags = ctx->groups[i].flags,
			.address = ctx->groups[i].address,
			.offset = ctx->groups[i].file_offset,
			.size = ctx->groups[i].size,
			.align = ctx->groups[i].align,
			.link = 0,
			.info = 0,
			.entry_size = es
		};
		if (strings_add(&shstr, sections[i + 1].name, &name_offsets[i + 1]) != 0)
			goto out_strings;
	}
	/* Set sh_link / sh_info for dynamic symbol table */
	if (ctx->shared) {
		/* Find .dynstr section index */
		uint32_t dynstr_sec = 0;
		uint32_t dynsym_sec = 0;
		for (i = 0; i < ctx->group_count; ++i) {
			const char *gn = ctx->groups[i].name;
			if (strcmp(gn, ".dynstr") == 0)
				dynstr_sec = (uint32_t)(i + 1);
			if (strcmp(gn, ".dynsym") == 0)
				dynsym_sec = (uint32_t)(i + 1);
		}
		if (dynsym_sec && dynstr_sec) {
			sections[dynsym_sec].link = dynstr_sec;
			sections[dynsym_sec].info = 1; /* first non-local symbol index */
		}
	}
	/* ---- .note.gnu.build-id section (if --build-id) ---- */
	if (ctx->build_id) {
		uint64_t hash = 0xcbf29ce484222325ULL;
		const uint64_t prime = 0x100000001b3ULL;
		uint64_t j;
		for (j = 0; j < ctx->group_count; ++j) {
			struct ld_group *g = &ctx->groups[j];
			if (g->type == MT_SHT_NOBITS || g->size == 0) continue;
			uint64_t k;
			for (k = 0; k < g->size; ++k) {
				hash ^= g->data[k];
				hash *= prime;
			}
		}
		unsigned char note_data[24];
		memset(note_data, 0, sizeof(note_data));
		write32(note_data + 0, 4);
		write32(note_data + 4, 8);
		write32(note_data + 8, 3);
		memcpy(note_data + 12, "GNU", 4);
		write64(note_data + 16, hash);
		uint32_t bid_sec_idx = (uint32_t)(ctx->group_count + 1);
		sections[bid_sec_idx].name = ".note.gnu.build-id";
		sections[bid_sec_idx].type = MT_SHT_NOTE;
		sections[bid_sec_idx].flags = LD_SHF_ALLOC;
		sections[bid_sec_idx].offset = file_end;
		sections[bid_sec_idx].address = base_addr + file_end;
		sections[bid_sec_idx].size = sizeof(note_data);
		sections[bid_sec_idx].align = 4;
		sections[bid_sec_idx].link = 0;
		sections[bid_sec_idx].info = 0;
		if (strings_add(&shstr, ".note.gnu.build-id",
		                &name_offsets[bid_sec_idx]) != 0)
			goto out_strings;
		file_end += sizeof(note_data);
	}
	/* ---- .eh_frame_hdr section (if --eh-frame-hdr and .eh_frame exists) ---- */
	if (ctx->eh_frame_hdr) {
		int ehg = find_group(ctx, ".eh_frame");
		if (ehg >= 0 && ctx->groups[ehg].size > 0 &&
		    ctx->groups[ehg].type != MT_SHT_NOBITS) {
			uint64_t eh_addr = base_addr + file_end;
			uint32_t eh_idx = (uint32_t)(ctx->group_count + 1 +
			                             (ctx->build_id ? 1 : 0));
			/* Generate eh_frame_hdr data directly with final encoded values */
			if (build_eh_frame_hdr(ctx, &eh_frame_hdr_data,
			                       &eh_frame_hdr_size, eh_addr) != 0)
				goto out_strings;
			sections[eh_idx].name = ".eh_frame_hdr";
			sections[eh_idx].type = MT_SHT_PROGBITS;
			sections[eh_idx].flags = LD_SHF_ALLOC;
			sections[eh_idx].offset = file_end;
			sections[eh_idx].address = eh_addr;
			sections[eh_idx].size = eh_frame_hdr_size;
			sections[eh_idx].align = 4;
			sections[eh_idx].link = 0;
			sections[eh_idx].info = 0;
			if (strings_add(&shstr, ".eh_frame_hdr",
			                &name_offsets[eh_idx]) != 0)
				goto out_strings;
			file_end += eh_frame_hdr_size;
		}
	}
	alloc_file_end = file_end;
	shstr_index = (uint32_t)(output_count - 1);
	sections[shstr_index].name = ".shstrtab";
	sections[shstr_index].type = MT_SHT_STRTAB;
	sections[shstr_index].flags = 0;
	sections[shstr_index].align = 1;
	sections[shstr_index].offset = file_end;
	sections[shstr_index].size = shstr.size;
	if (strings_add(&shstr, ".shstrtab", &name_offsets[shstr_index]) != 0)
		goto out_strings;
	sections[shstr_index].size = shstr.size;
	file_end = align_up(file_end, 1) + shstr.size;
	section_offset = align_up(file_end, 8);
	file = fopen(path, "wb+");
	if (!file)
		goto out_strings;
	/* ELF header — set fields from target descriptor. */
	{
	unsigned char h[64] = {0x7f, 'E', 'L', 'F',
	                       target->elf_class, target->elf_endian, 1, 0};
		uint16_t e_type = (ctx->shared || ctx->pie) ? MT_ET_DYN : MT_ET_EXEC;
		write16(h + 16, e_type);
		write16(h + 18, target->emachine);
		write32(h + 20, 1);
		write64(h + 24, entry_address);
		write64(h + 32, target->ehdr_size);
		write64(h + 40, section_offset);
		write32(h + 48, target->e_flags);
		write16(h + 52, target->ehdr_size);
		write16(h + 54, 56);
		phnum = 1; /* PT_LOAD */
		if (ctx->tls_size) phnum++;
		if (ctx->shared) phnum += 2; /* PT_PHDR + PT_DYNAMIC */
		if (ctx->pie) phnum += 1;    /* PT_INTERP */
		write16(h + 56, phnum);
		write16(h + 58, target->shdr_size);
		write16(h + 60, (uint16_t)output_count);
		write16(h + 62, (uint16_t)shstr_index);
		if (fwrite(h, 1, sizeof(h), file) != sizeof(h))
			goto out_file;
	}
	/* QEMU 7.2 loongarch64 user-mode loader has a bug: it crashes
	 * (SEGV_ACCERR) when executing code from the first LOAD segment
	 * if a second non-empty PT_LOAD exists.  Workaround: emit a single
	 * LOAD segment covering both RX and RW data with RWX permissions.
	 *
	 * Set FileSiz to load_file_end (non-NOBITS data only) so the kernel
	 * zero-fills the BSS area (between load_file_end and memory_end). */
	{
		uint64_t single_end = memory_end > rx_end ? memory_end : rx_end;
		uint64_t filesz = alloc_file_end > LD_PAGE ? alloc_file_end : LD_PAGE;
		uint64_t memsz = single_end > LD_PAGE ? single_end : LD_PAGE;
		/* Extend LOAD FileSiz to cover .tdata so __meuos_tls_init
		 * can read the TLS initial values via the LOAD mapping. */
		if (ctx->tls_tdata_group >= 0) {
			uint64_t tde = ctx->groups[ctx->tls_tdata_group].file_offset
			  + ctx->groups[ctx->tls_tdata_group].size;
			if (tde > filesz) filesz = tde;
			if (tde > memsz) memsz = tde;
		}
		if (write_program_header(file, LD_PF_R | LD_PF_W | LD_PF_X,
		                         0, base_addr, filesz, memsz) != 0)
			goto out_file;
	}
	/* PT_PHDR and PT_DYNAMIC for shared libraries (ET_DYN) */
	if (ctx->shared) {
		/* PT_PHDR: describes the program header table itself. The
		 * table sits right after the ELF header (file offset = ehdr_size)
		 * and is mapped at base_addr + ehdr_size by the single LOAD. */
		uint64_t phoff = target->ehdr_size;
		if (write_program_header_type(file, MT_PT_PHDR, LD_PF_R,
		                             phoff, base_addr + phoff,
		                             (uint64_t)phnum * 56,
		                             (uint64_t)phnum * 56, 8) != 0)
			goto out_file;
		/* PT_DYNAMIC: points at the .dynamic section created above. */
		int dg = find_group(ctx, ".dynamic");
		if (dg >= 0) {
			struct ld_group *dyn = &ctx->groups[dg];
			if (write_program_header_type(file, MT_PT_DYNAMIC,
			                             LD_PF_R | LD_PF_W,
			                             dyn->file_offset, dyn->address,
			                             dyn->size, dyn->size, 8) != 0)
				goto out_file;
		}
	}
	/* PT_INTERP for PIE executables */
	if (ctx->pie) {
		int ig = find_group(ctx, ".interp");
		if (ig >= 0) {
			struct ld_group *interp = &ctx->groups[ig];
			if (write_program_header_type(file, MT_PT_INTERP, LD_PF_R,
			                             interp->file_offset, interp->address,
			                             interp->size, interp->size, 1) != 0)
				goto out_file;
		}
	}
	if (ctx->tls_size) {
		uint64_t tls_addr = 0, tls_off = 0, tls_filesz = 0;
		if (ctx->tls_tdata_group >= 0) {
			tls_addr = ctx->groups[ctx->tls_tdata_group].address;
			tls_off = ctx->groups[ctx->tls_tdata_group].file_offset;
			tls_filesz = ctx->groups[ctx->tls_tdata_group].size;
		} else if (ctx->tls_tbss_group >= 0) {
			/* .tbss-only TLS: emit PT_TLS with filesz=0.
			 * The .tbss group has a valid address beyond the LOAD
			 * segments (layout pass allocates it at LD_BASE+offset),
			 * so PT_TLS does NOT overlap PT_LOAD.  The old workaround
			 * (goto skip_tls) was for QEMU 7.2 loongarch64 which
			 * rejected overlapping PT_TLS with PT_LOAD; with proper
			 * .tbss address assignment this no longer occurs. */
			tls_addr = ctx->groups[ctx->tls_tbss_group].address;
			tls_off = ctx->groups[ctx->tls_tbss_group].file_offset;
		}
		if (write_program_header_type(file, LD_PT_TLS, LD_PF_R, tls_off,
		                               tls_addr, tls_filesz, ctx->tls_size,
		                               ctx->tls_align > 0x1000 ? ctx->tls_align : 0x1000) != 0)
			goto out_file;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (group->type == MT_SHT_NOBITS || group->size == 0)
			continue;
		if (fseek(file, (long)group->file_offset, SEEK_SET) != 0 ||
		    fwrite(group->data, 1, group->size, file) != group->size)
			goto out_file;
	}
	if (ctx->build_id) {
		unsigned char note_data[24];
		memset(note_data, 0, sizeof(note_data));
		/* Recompute hash (same as above, no clean way to save across the section setup) */
		uint64_t hash = 0xcbf29ce484222325ULL;
		const uint64_t prime = 0x100000001b3ULL;
		uint64_t j;
		for (j = 0; j < ctx->group_count; ++j) {
			struct ld_group *g = &ctx->groups[j];
			if (g->type == MT_SHT_NOBITS || g->size == 0) continue;
			uint64_t k;
			for (k = 0; k < g->size; ++k) {
				hash ^= g->data[k];
				hash *= prime;
			}
		}
		write32(note_data + 0, 4);
		write32(note_data + 4, 8);
		write32(note_data + 8, 3);
		memcpy(note_data + 12, "GNU", 4);
		write64(note_data + 16, hash);
		uint32_t bid_idx = (uint32_t)(ctx->group_count + 1);
		if (fseek(file, (long)sections[bid_idx].offset, SEEK_SET) != 0 ||
		    fwrite(note_data, 1, 24, file) != 24)
			goto out_file;
	}
	/* Write .eh_frame_hdr data (pre-encoded, no patching needed) */
	if (eh_frame_hdr_data) {
		uint32_t eh_idx = (uint32_t)(ctx->group_count + 1 +
		                             (ctx->build_id ? 1 : 0));
		if (fseek(file, (long)sections[eh_idx].offset, SEEK_SET) != 0 ||
		    fwrite(eh_frame_hdr_data, 1, eh_frame_hdr_size, file) != eh_frame_hdr_size)
			goto out_file;
	}
	if (fseek(file, (long)sections[shstr_index].offset, SEEK_SET) != 0 ||
	    fwrite(shstr.data, 1, shstr.size, file) != shstr.size)
		goto out_file;
	if (fseek(file, (long)section_offset, SEEK_SET) != 0)
		goto out_file;
	if (fwrite((unsigned char[64]){0}, 1, 64, file) != 64)
		goto out_file;
	for (i = 1; i < (size_t)output_count; ++i) {
		unsigned char sh[64] = {0};
		write32(sh + 0, name_offsets[i]);
		write32(sh + 4, sections[i].type);
		write64(sh + 8, sections[i].flags);
		write64(sh + 16, sections[i].address);
		write64(sh + 24, sections[i].offset);
		write64(sh + 32, sections[i].size);
		write32(sh + 40, sections[i].link);
		write32(sh + 44, sections[i].info);
		write64(sh + 48, sections[i].align ? sections[i].align : 1);
		write64(sh + 56, sections[i].entry_size);
		if (fwrite(sh, 1, sizeof(sh), file) != sizeof(sh))
			goto out_file;
	}
	if (fclose(file) != 0) {
		file = NULL;
		goto out_strings;
	}
	file = NULL;
	if (chmod(path, 0755) != 0)
		goto out_strings;
	result = 0;
out_file:
	if (file)
		fclose(file);
out_strings:
	free(shstr.data);
out:
	free(eh_frame_hdr_data);
	free(sections);
	free(name_offsets);
	return result;
}

/* ---- Dynamic section helpers for -shared output ---- */

/* Return the smallest prime >= n. */
static uint32_t
next_prime32(uint32_t n)
{
	uint32_t p, i;
	for (p = n < 2 ? 2 : n; ; ++p) {
		int isp = 1;
		for (i = 2; i * i <= p; ++i) {
			if (p % i == 0) { isp = 0; break; }
		}
		if (isp) return p;
	}
}

/* Standard ELF-32 hash (used for both 32 and 64 bit .hash). */
static uint32_t
elf_hash32(const char *name)
{
	uint32_t h = 0, g;
	while (*name) {
		h = (h << 4) + (unsigned char)*name++;
		if ((g = h & 0xf0000000u))
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/* Write an ElfXX_Sym entry at p (64 or 32 bit depending on elf64). */
static void
write_dynsym_entry(unsigned char *p, int elf64,
                   uint32_t name, uint8_t info, uint8_t other,
                   uint16_t shndx, uint64_t value, uint64_t size)
{
	if (elf64) {
		write32(p + 0, name);
		p[4] = info;
		p[5] = other;
		write16(p + 6, shndx);
		write64(p + 8, value);
		write64(p + 16, size);
	} else {
		write32(p + 0, name);
		write32(p + 4, (uint32_t)value);
		write32(p + 8, (uint32_t)size);
		p[12] = info;
		p[13] = other;
		write16(p + 14, shndx);
	}
}

/* Pass 1: collect exported globals, create .dynsym / .dynstr / .hash groups
 * and fill their content (except symbol values which need layout addresses). */
static int
build_dynamic_tables(struct ld_context *ctx)
{
	size_t i;
	size_t nsym = 0;

	if (!ctx->shared)
		return 0;

	/* Count exported (defined non-weak) globals with names */
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *g = &ctx->globals.items[i];
		if (g->defined && !g->weak && g->name)
			++nsym;
	}

	/* Allocate local dynsym metadata array */
	if (nsym > ctx->dynsym_capacity) {
		size_t cap = ctx->dynsym_capacity ? ctx->dynsym_capacity * 2 : 64;
		while (cap < nsym) cap *= 2;
		struct ld_dynsym_entry *e = (struct ld_dynsym_entry *)ld_realloc(
		    ctx->dynsym_entries, cap * sizeof(*e));
		if (!e) return ld_error(ctx, "out of memory");
		ctx->dynsym_entries = e;
		ctx->dynsym_capacity = cap;
	}
	ctx->dynsym_count = nsym;

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t symsize = elf64 ? MT_ELF64_SYM_SIZE : MT_ELF32_SYM_SIZE;

	/* ---- .dynstr ---- */
	int dg_str = get_group(ctx, ".dynstr", MT_SHT_STRTAB,
	                       LD_SHF_ALLOC, 1);
	if (dg_str < 0) return ld_error(ctx, "out of memory");
	struct ld_group *gstr = &ctx->groups[dg_str];
	/* Leading NUL byte (string table index 0 = empty string) */
	uint64_t stroff;
	if (append_group_data(ctx, gstr, (const unsigned char *)"", 1, 1, &stroff) != 0)
		return -1;

	/* Store soname string (if any) and append its name to .dynstr */
	ctx->soname_dynstr_offset = 0;
	if (ctx->soname) {
		uint64_t so_off;
		if (append_group_data(ctx, gstr,
		                      (const unsigned char *)ctx->soname,
		                      strlen(ctx->soname) + 1, 1, &so_off) != 0)
			return -1;
		ctx->soname_dynstr_offset = (uint32_t)so_off;
	}

	/* ---- .dynsym (placeholder, filled in pass 2) ---- */
	int dg_sym = get_group(ctx, ".dynsym", MT_SHT_DYNSYM,
	                       LD_SHF_ALLOC, 8);
	if (dg_sym < 0) return ld_error(ctx, "out of memory");
	struct ld_group *gsym = &ctx->groups[dg_sym];
	size_t nsym_total = nsym + 1; /* index 0 = null symbol */
	size_t dysize = nsym_total * symsize;
	/* Zero-fill the entire .dynsym (null symbol at 0 stays zero) */
	unsigned char *dz = (unsigned char *)ld_malloc(dysize ? dysize : 1);
	if (!dz) return ld_error(ctx, "out of memory");
	memset(dz, 0, dysize);
	uint64_t sym_data_off;
	if (append_group_data(ctx, gsym, dz, dysize, 8, &sym_data_off) != 0) {
		free(dz);
		return -1;
	}
	free(dz);
	ctx->dynsym_data_offset = sym_data_off;

	/* Now populate the dynsym_entries metadata (name → dynstr_offset).
	 *
	 * IMPORTANT: this MUST happen BEFORE building the .hash table,
	 * because the hash loop accesses e->global->name. */
	size_t ei = 0;
	for (i = 0; i < ctx->globals.count; ++i) {
		struct ld_global *g = &ctx->globals.items[i];
		if (!(g->defined && !g->weak) || !g->name)
			continue;
		/* Determine st_type */
		int stt = MT_STT_OBJECT;
		if (g->group >= 0) {
			const char *gname = ctx->groups[g->group].name;
			if (gname && strcmp(gname, ".text") == 0)
				stt = MT_STT_FUNC;
			else if (g->group >= 0 &&
			         (g->group == ctx->tls_tdata_group ||
			          g->group == ctx->tls_tbss_group))
				stt = MT_STT_TLS;
		}
		uint32_t dynstr_off;
		if (append_group_data(ctx, gstr,
		                      (const unsigned char *)g->name,
		                      strlen(g->name) + 1, 1, &stroff) != 0)
			return -1;
		dynstr_off = (uint32_t)stroff;
		ctx->dynsym_entries[ei].global = g;
		ctx->dynsym_entries[ei].dynstr_offset = dynstr_off;
		ctx->dynsym_entries[ei].stt = stt;
		++ei;
	}

	/* ---- .hash (SysV format, 32-bit entries) ---- */
	int dg_hash = get_group(ctx, ".hash", MT_SHT_HASH,
	                        LD_SHF_ALLOC, 4);
	if (dg_hash < 0) return ld_error(ctx, "out of memory");
	struct ld_group *ghash = &ctx->groups[dg_hash];
	uint32_t nbucket = next_prime32((uint32_t)(nsym_total > 1 ? nsym_total : 3));
	uint32_t nchain = (uint32_t)nsym_total;
	size_t hash_size = (size_t)(2 + nbucket + nchain) * 4;
	unsigned char *hb = (unsigned char *)ld_malloc(hash_size);
	if (!hb) return ld_error(ctx, "out of memory");
	memset(hb, 0, hash_size);
	write32(hb, nbucket);
	write32(hb + 4, nchain);
	uint32_t *bucket = (uint32_t *)(hb + 8);
	uint32_t *chain = (uint32_t *)(hb + 8 + (size_t)nbucket * 4);
	/* For each exported symbol (index 1..nsym in .dynsym) */
	for (i = 0; i < nsym; ++i) {
		struct ld_dynsym_entry *e = &ctx->dynsym_entries[i];
		uint32_t h = elf_hash32(e->global->name);
		uint32_t bi = h % nbucket;
		uint32_t sym_ndx = (uint32_t)(i + 1);
		chain[sym_ndx] = bucket[bi];
		bucket[bi] = sym_ndx;
	}
	uint64_t hash_off;
	if (append_group_data(ctx, ghash, hb, hash_size, 4, &hash_off) != 0) {
		free(hb);
		return -1;
	}
	free(hb);

	/* ---- .dynamic (placeholder with correct size, filled in pass 2) ---- */
	size_t ntags = 5; /* SYMTAB, SYMENT, STRTAB, STRSZ, HASH */
	if (ctx->soname) ntags++;
	/* Pre-create .rela.dyn group for dynamic relocations (must exist before
	 * layout_output assigns addresses).  Content is filled by build_rela_dyn()
	 * after layout. */
	int have_rela = (ctx->shared && ctx->got.count > 0);
	if (have_rela) {
		ntags += 3; /* DT_RELA, DT_RELASZ, DT_RELAENT */
		int rg = get_group(ctx, ".rela.dyn", MT_SHT_RELA,
		                   LD_SHF_ALLOC, 8);
		if (rg < 0)
			return ld_error(ctx, "out of memory");
		ctx->groups[rg].rank = 1; /* same rank as .rodata/dynstr/dynsym */
	}
	ntags++; /* DT_NULL terminator */
	size_t dynent = elf64 ? 16 : 8;
	size_t dyn_total = ntags * dynent;
	int dg_dyn = find_group(ctx, ".dynamic"); /* created by ensure_dynamic_section */
	if (dg_dyn < 0) return ld_error(ctx, "out of memory");
	struct ld_group *gdyn = &ctx->groups[dg_dyn];
	unsigned char *dd = (unsigned char *)ld_malloc(dyn_total ? dyn_total : 1);
	if (!dd) return ld_error(ctx, "out of memory");
	memset(dd, 0, dyn_total);
	uint64_t dyn_off;
	if (append_group_data(ctx, gdyn, dd, dyn_total, 8, &dyn_off) != 0) {
		free(dd);
		return -1;
	}
	free(dd);

	return 0;
}

/* Pass 2: after layout, fill .dynsym entry values and .dynamic DT entries.
 * Called after layout_output and before write_executable. */
static int
fill_dynamic_addresses(struct ld_context *ctx)
{
	size_t i;

	if (!ctx->shared)
		return 0;

	int elf64 = (ctx->target->elf_class == MT_ELFCLASS64);
	size_t symsize = elf64 ? MT_ELF64_SYM_SIZE : MT_ELF32_SYM_SIZE;

	int dg_sym = find_group(ctx, ".dynsym");
	int dg_str = find_group(ctx, ".dynstr");
	int dg_hash = find_group(ctx, ".hash");
	int dg_dyn = find_group(ctx, ".dynamic");
	if (dg_sym < 0 || dg_str < 0 || dg_hash < 0 || dg_dyn < 0)
		return ld_error(ctx, "internal: dynamic sections not created");

	struct ld_group *gsym = &ctx->groups[dg_sym];
	struct ld_group *gstr = &ctx->groups[dg_str];
	struct ld_group *ghash = &ctx->groups[dg_hash];
	struct ld_group *gdyn = &ctx->groups[dg_dyn];

	uint64_t sym_addr   = gsym->address;
	uint64_t str_addr   = gstr->address;
	uint64_t hash_addr  = ghash->address;

	/* Fill .dynsym entries (skip null symbol at index 0) */
	for (i = 0; i < ctx->dynsym_count; ++i) {
		struct ld_dynsym_entry *e = &ctx->dynsym_entries[i];
		struct ld_global *g = e->global;
		uint64_t st_value = ctx->groups[g->group].address + g->offset;
		uint16_t st_shndx = (uint16_t)(g->group + 1); /* output section index */
		uint8_t info = (uint8_t)((LD_STB_GLOBAL << LD_STB_SHIFT) | e->stt);
		unsigned char *p = gsym->data + ctx->dynsym_data_offset +
		                   (i + 1) * symsize;
		write_dynsym_entry(p, elf64, e->dynstr_offset, info, 0,
		                   st_shndx, st_value, g->size);
	}

	/* Fill .dynamic DT entries in the order reserved by build_dynamic_tables:
	 *   SYMTAB, SYMENT, STRTAB, STRSZ, HASH, [SONAME,] NULL */
	size_t k = 0;
	unsigned char *dp = gdyn->data;

	/* DT_SYMTAB */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_SYMTAB);
		write64(dp + k * 16 + 8, sym_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_SYMTAB);
		write32(dp + k * 8 + 4, (uint32_t)sym_addr);
	}
	++k;

	/* DT_SYMENT */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_SYMENT);
		write64(dp + k * 16 + 8, (uint64_t)symsize);
	} else {
		write32(dp + k * 8 + 0, MT_DT_SYMENT);
		write32(dp + k * 8 + 4, (uint32_t)symsize);
	}
	++k;

	/* DT_STRTAB */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_STRTAB);
		write64(dp + k * 16 + 8, str_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_STRTAB);
		write32(dp + k * 8 + 4, (uint32_t)str_addr);
	}
	++k;

	/* DT_STRSZ */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_STRSZ);
		write64(dp + k * 16 + 8, (uint64_t)gstr->size);
	} else {
		write32(dp + k * 8 + 0, MT_DT_STRSZ);
		write32(dp + k * 8 + 4, (uint32_t)gstr->size);
	}
	++k;

	/* DT_HASH */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_HASH);
		write64(dp + k * 16 + 8, hash_addr);
	} else {
		write32(dp + k * 8 + 0, MT_DT_HASH);
		write32(dp + k * 8 + 4, (uint32_t)hash_addr);
	}
	++k;

	/* DT_SONAME (optional) */
	if (ctx->soname) {
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_SONAME);
			write64(dp + k * 16 + 8, ctx->soname_dynstr_offset);
		} else {
			write32(dp + k * 8 + 0, MT_DT_SONAME);
			write32(dp + k * 8 + 4, ctx->soname_dynstr_offset);
		}
		++k;
	}

	/* DT_RELA / DT_RELASZ / DT_RELAENT (for shared libraries with GOT) */
	int dg_rela = find_group(ctx, ".rela.dyn");
	if (dg_rela >= 0) {
		struct ld_group *grela = &ctx->groups[dg_rela];
		/* Compute RELASZ from GOT entries (each GOT entry = 1 RELA entry).
		 * The .rela.dyn data is filled by build_rela_dyn() AFTER this
		 * function runs, so we compute the expected size here. */
		uint64_t rela_entsize = elf64 ? 24 : 12;
		uint64_t rela_sz = ctx->got.count * rela_entsize;
		/* DT_RELA: address of .rela.dyn */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELA);
			write64(dp + k * 16 + 8, grela->address);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELA);
			write32(dp + k * 8 + 4, (uint32_t)grela->address);
		}
		++k;
		/* DT_RELASZ: size of .rela.dyn (computed from GOT entry count) */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELASZ);
			write64(dp + k * 16 + 8, rela_sz);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELASZ);
			write32(dp + k * 8 + 4, (uint32_t)rela_sz);
		}
		++k;
		/* DT_RELAENT: size of each RELA entry */
		if (elf64) {
			write64(dp + k * 16 + 0, MT_DT_RELAENT);
			write64(dp + k * 16 + 8, rela_entsize);
		} else {
			write32(dp + k * 8 + 0, MT_DT_RELAENT);
			write32(dp + k * 8 + 4, (uint32_t)rela_entsize);
		}
		++k;
	}

	/* DT_NULL terminator is already zeroed in the placeholder — but write
	 * it explicitly for clarity. */
	if (elf64) {
		write64(dp + k * 16 + 0, MT_DT_NULL);
		write64(dp + k * 16 + 8, 0);
	} else {
		write32(dp + k * 8 + 0, MT_DT_NULL);
		write32(dp + k * 8 + 4, 0);
	}
	return 0;
}

/* Ensure the .dynamic output section group exists for -shared.
 * Actual content (DT entries) is built later by build_dynamic_tables /
 * fill_dynamic_addresses. */
static int
ensure_dynamic_section(struct ld_context *ctx)
{
	if (!ctx->shared)
		return 0;
	int g = get_group(ctx, ".dynamic", MT_SHT_PROGBITS,
	                  LD_SHF_ALLOC | LD_SHF_WRITE, 8);
	if (g < 0)
		return ld_error(ctx, "out of memory");
	ctx->groups[g].rank = 2; /* same rank as .got, inside the RW region */
	return 0;
}

/* For PIE executables, create the .interp section that holds the path to
 * the dynamic linker.  The content is fixed (/lib/ld-meuos.so.1) so we
 * fill it immediately; the section just needs to exist before layout(). */
static int
ensure_pie_section(struct ld_context *ctx)
{
	if (!ctx->pie)
		return 0;
	int g = get_group(ctx, ".interp", MT_SHT_PROGBITS,
	                  LD_SHF_ALLOC, 1);
	if (g < 0)
		return ld_error(ctx, "out of memory");
	ctx->groups[g].rank = 1; /* read-only region */
	const char *interp = "/lib/ld-meuos.so.1";
	uint64_t off;
	if (append_group_data(ctx, &ctx->groups[g],
	                       (const unsigned char *)interp,
	                       strlen(interp) + 1, 1, &off) != 0)
		return -1;
	return 0;
}

/* --gc-sections: garbage-collect unreferenced output groups.
 * Called after collect_symbols() and extract_archives().
 * Marks groups reachable from entry and .init_array/.fini_array
 * as kept; all others have their data removed. */
static int
gc_sweep(struct ld_context *ctx, const char *entry_name)
{
	size_t i;
	int changed;

	/* Mark entry point's section and root sections */
	if (entry_name) {
		struct ld_global *entry_sym = find_global(ctx, entry_name);
		if (entry_sym && entry_sym->defined && entry_sym->group >= 0)
			ctx->groups[entry_sym->group].kept = 1;
	}
	for (i = 0; i < ctx->group_count; i++) {
		if (strcmp(ctx->groups[i].name, ".init_array") == 0 ||
		    strcmp(ctx->groups[i].name, ".fini_array") == 0 ||
		    strcmp(ctx->groups[i].name, ".init") == 0 ||
		    strcmp(ctx->groups[i].name, ".fini") == 0)
			ctx->groups[i].kept = 1;
	}

	/* Propagate: walk relocations from kept sections */
	do {
		changed = 0;
		for (i = 0; i < ctx->objects.count; i++) {
			struct ld_object *obj = &ctx->objects.items[i];
			uint16_t k;
			for (k = 0; k < object_section_count(obj); k++) {
				struct mt_elf64_section sec;
				if (object_get_section(obj, k, &sec) != 0) continue;
				if (sec.type != MT_SHT_RELA) continue;
				int target_idx = (int)(unsigned)sec.info;
				if (target_idx < 0 ||
				    (size_t)target_idx >= object_section_count(obj)) continue;
				int tg = obj->maps[target_idx].group;
				if (tg < 0 || tg >= (int)ctx->group_count) continue;
				if (!ctx->groups[tg].kept) continue;
				/* This relocation section targets a kept group.
				 * Walk individual relocations to find referenced sections */
				size_t r;
				for (r = 0; r < sec.size / sec.entry_size; r++) {
					const unsigned char *p = obj->data + sec.offset +
					    r * sec.entry_size;
					uint64_t info;
					uint64_t sym_idx;
					struct mt_elf64_symbol sym;
					if (obj->elf_class == 1) {
						info = read32(p + 4);
						sym_idx = info >> 8;
					} else {
						info = read64(p + 8);
						sym_idx = info >> 32;
					}
					if (object_get_symbol(obj, sym_idx, &sym) != 0) continue;
					if (sym.section > 0 && sym.section < object_section_count(obj)) {
						int sg = obj->maps[sym.section].group;
						if (sg >= 0 && sg < (int)ctx->group_count && !ctx->groups[sg].kept) {
							ctx->groups[sg].kept = 1;
							changed = 1;
						}
					}
				}
			}
		}
	} while (changed);

	/* Remove unkept allocatable sections (text/data/bss sub-sections) */
	for (i = 0; i < ctx->group_count; i++) {
		if (ctx->groups[i].kept) continue;
		/* Keep non-allocatable sections (.comment, .note, debug) as-is */
		if (!(ctx->groups[i].flags & LD_SHF_ALLOC)) continue;
		ctx->groups[i].size = 0;
		ctx->groups[i].type = MT_SHT_NOBITS;
		free(ctx->groups[i].data);
		ctx->groups[i].data = NULL;
	}

	return 0;
}

int
mt_ld_link_opts(const struct mt_ld_options *opts,
                const char *const *inputs, size_t input_count,
                const char *target_name,
                const char **error_message)
{
	struct ld_context ctx;
	size_t i;
	int result = -1;
	memset(&ctx, 0, sizeof(ctx));
	ctx.tls_tdata_group = -1;
	ctx.tls_tbss_group = -1;
	ctx.tls_align = 1;
	if (target_name) {
		ctx.target = mt_target_lookup(target_name);
		if (!ctx.target) {
			if (error_message)
				*error_message = "unsupported target architecture";
			return 1;
		}
	} else {
		ctx.target = &mt_target_x86_64;
	}
	if (opts) {
		ctx.shared = opts->shared;
		ctx.pie = opts->pie;
		ctx.soname = opts->soname;
		ctx.build_id = opts->build_id;
		ctx.eh_frame_hdr = opts->eh_frame_hdr;
		ctx.as_needed = opts->as_needed;
		ctx.whole_archive = opts->whole_archive;
		ctx.no_undefined = opts->no_undefined;
		ctx.gc_sections = opts->gc_sections;
		ctx.print_map = opts->print_map;
		ctx.link_script = opts->link_script;
		/* Default: --no-as-needed when unset */
		if (ctx.as_needed < 0)
			ctx.as_needed = 0;
	}
	ctx.got.group = -1;
	for (i = 0; i < input_count; ++i)
		if (load_input(&ctx, inputs[i]) != 0)
			goto out;
	if (ctx.objects.count == 0) {
		ld_error(&ctx, "no object files were loaded");
		goto out;
	}
	if (collect_sections(&ctx) != 0 || ensure_dynamic_section(&ctx) != 0 ||
	    ensure_pie_section(&ctx) != 0 ||
	    collect_symbols(&ctx) != 0 ||
	    extract_archives(&ctx) != 0 || allocate_common(&ctx) != 0 ||
	    collect_got_relocations(&ctx) != 0)
		goto out;
	/* --defsym: define absolute symbols (must run before --no-undefined
	 * so the new symbols are treated as satisfied references). */
	if (opts && opts->defsym) {
		size_t di;
		for (di = 0; di < opts->defsym_count; ++di) {
			if (apply_defsym(&ctx, opts->defsym[di]) != 0)
				goto out;
		}
	}
	/* --wrap: redirect references to SYM to __wrap_SYM.
	 * Must run after all symbols are collected (including from archives)
	 * but before --no-undefined and relocation resolution. */
	if (opts && opts->wrap) {
		size_t wi;
		for (wi = 0; wi < opts->wrap_count; ++wi) {
			if (apply_wrap(&ctx, opts->wrap[wi]) != 0)
				goto out;
		}
	}
	/* --no-undefined: check for unresolved symbols */
	if (ctx.no_undefined) {
		size_t ui;
		for (ui = 0; ui < ctx.globals.count; ++ui) {
			struct ld_global *ug = &ctx.globals.items[ui];
			/* Follow alias chain: a wrapped symbol redirects to __wrap_SYM,
			 * so check the alias target's defined status. */
			while (ug->alias)
				ug = ug->alias;
			if (!ug->defined) {
				ld_errorf(&ctx, "undefined reference to",
				          ctx.globals.items[ui].name);
				goto out;
			}
		}
	}
	if (ctx.gc_sections) {
		if (gc_sweep(&ctx, opts ? opts->entry : "_start") != 0)
			goto out;
	}
	if (build_dynamic_tables(&ctx) != 0)
		goto out;
	if (ctx.got.count != 0)
		ctx.got.group = find_group(&ctx, ".got");
	/* Apply link script (if provided) to adjust section ordering */
	if (ctx.link_script) {
		if (apply_link_script(&ctx, ctx.link_script) != 0)
			goto out;
	}
	if (layout_output(&ctx) != 0 || fill_dynamic_addresses(&ctx) != 0 ||
	    fill_got(&ctx) != 0 ||
	    build_rela_dyn(&ctx) != 0 ||
	    apply_relocations(&ctx) != 0 ||
	    write_executable(&ctx, opts ? opts->output : "a.out",
	                     opts ? opts->entry : "_start",
	                     ctx.target) != 0)
		goto out;
	/* --print-map: output section layout and symbol values */
	if (ctx.print_map) {
		fprintf(stderr, "\nLink map:\n");
		fprintf(stderr, "%-20s %-18s %-10s %s\n", "Section", "Address", "Size", "File offset");
		size_t mi;
		for (mi = 0; mi < ctx.group_count; mi++) {
			if (ctx.groups[mi].type == MT_SHT_NOBITS && ctx.groups[mi].size == 0) continue;
			if (ctx.groups[mi].flags & LD_SHF_ALLOC || ctx.groups[mi].size > 0) {
				fprintf(stderr, "%-20s 0x%016llx %-10llu %-10llu\n",
				        ctx.groups[mi].name,
				        (unsigned long long)ctx.groups[mi].address,
				        (unsigned long long)ctx.groups[mi].size,
				        (unsigned long long)ctx.groups[mi].file_offset);
			}
		}
		/* Print entry point */
		if (opts && opts->entry) {
			struct ld_global *es = find_global(&ctx, opts->entry);
			if (es && es->defined) {
				uint64_t ea = ctx.groups[es->group].address + es->offset;
				fprintf(stderr, "Entry: %s = 0x%llx\n", opts->entry,
				        (unsigned long long)ea);
			}
		}
	}
	result = 0;
out:
	if (result != 0) {
		static char reported_error[512];
		if (!ctx.error[0])
			strcpy(ctx.error, "link failed");
		strncpy(reported_error, ctx.error, sizeof(reported_error) - 1);
		reported_error[sizeof(reported_error) - 1] = '\0';
		if (error_message)
			*error_message = reported_error;
	}
	free_context(&ctx);
	return result;
}

int
mt_ld_link(const char *output, const char *entry,
           const char *const *inputs, size_t input_count,
           const char *target_name,
           const char **error_message)
{
	struct mt_ld_options opts = {0};
	opts.output = output;
	opts.entry = entry;
	opts.shared = 0;
	return mt_ld_link_opts(&opts, inputs, input_count, target_name,
	                       error_message);
}
