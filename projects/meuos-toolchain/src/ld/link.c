/* link.c - static ET_REL -> ET_EXEC linker. */
#include "mt/ld.h"
#include "mt/archive.h"
#include "mt/elf.h"
#include "mt/target.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
};

struct ld_section_map {
	int group;
	uint64_t offset;
};

struct ld_object {
	char *name;
	unsigned char *data;
	size_t size;
	struct mt_elf64_view view;
	struct ld_section_map *maps;
	uint16_t symtab_index;
	struct mt_elf64_section symtab;
	struct mt_elf64_section strtab;
	struct mt_elf64_section section_names;
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
	memset(ctx, 0, sizeof(*ctx));
}

static int
append_object(struct ld_context *ctx, const char *name,
              const unsigned char *data, size_t size)
{
	struct ld_object *objects;
	struct ld_object *object;
	enum mt_elf_status status;
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
	status = mt_elf64_parse(object->data, object->size, &object->view);
	if (status != MT_ELF_OK || object->view.type != MT_ET_REL ||
	    object->view.machine != ctx->target->emachine) {
		free_object(object);
		return ld_errorf(ctx, "unsupported input object", name);
	}
	object->maps = (struct ld_section_map *)ld_malloc(
	    object->view.section_count * sizeof(*object->maps));
	if (!object->maps && object->view.section_count != 0) {
		free_object(object);
		return ld_error(ctx, "out of memory");
	}
	for (uint16_t i = 0; i < object->view.section_count; ++i)
		object->maps[i].group = -1;
	if (object->view.section_name_index < object->view.section_count &&
	    mt_elf64_get_section(object->data, object->size, &object->view,
	                         object->view.section_name_index,
	                         &object->section_names) != MT_ELF_OK) {
		free_object(object);
		return ld_errorf(ctx, "invalid section-name table", name);
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

	file = fopen(path, "rb");
	if (!file)
		return -1;
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
	if (suffix && strcmp(suffix, ".a") == 0)
		return remember_archive(ctx, path);
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
	if (strcmp(name, ".rodata") == 0) return 1;
	if (strcmp(name, ".eh_frame") == 0) return 1;
	if (strcmp(name, ".got") == 0) return 2;
	if (strcmp(name, ".data") == 0 || strcmp(name, ".tdata") == 0) return 3;
	if (strcmp(name, ".bss") == 0 || strcmp(name, ".tbss") == 0) return 4;
	return 5;
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

static const char *
object_section_name(const struct ld_object *object, uint16_t index)
{
	struct mt_elf64_section section;
	const char *name;
	if (mt_elf64_get_section(object->data, object->size, &object->view,
	                         index, &section) != MT_ELF_OK ||
	    mt_elf64_get_string(object->data, object->size, &object->section_names,
	                        section.name, &name) != MT_ELF_OK)
		return NULL;
	return name;
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
		for (j = 0; j < object->view.section_count; ++j) {
			if (mt_elf64_get_section(object->data, object->size,
			                        &object->view, j, &section) != MT_ELF_OK)
				return ld_errorf(ctx, "invalid section in object", object->name);
			if (section.type != MT_SHT_PROGBITS &&
			    section.type != MT_SHT_NOBITS)
				continue;
			if (!(section.flags & LD_SHF_ALLOC))
				continue;
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
	for (j = 0; j < object->view.section_count; ++j) {
		if (mt_elf64_get_section(object->data, object->size,
		                        &object->view, j, &section) != MT_ELF_OK)
			return ld_errorf(ctx, "invalid section in object", object->name);
		if (section.type != MT_SHT_PROGBITS && section.type != MT_SHT_NOBITS)
			continue;
		if (!(section.flags & LD_SHF_ALLOC))
			continue;
		name = object_section_name(object, j);
		if (!name || !*name)
			return ld_errorf(ctx, "section has no name", object->name);
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
	object->has_symtab = 0;
	for (i = 0; i < object->view.section_count; ++i) {
		if (mt_elf64_get_section(object->data, object->size,
		                        &object->view, i, &section) != MT_ELF_OK)
			return ld_errorf(ctx, "invalid symbol section", object->name);
		if (section.type != MT_SHT_SYMTAB)
			continue;
		if (section.link >= object->view.section_count ||
		    mt_elf64_get_section(object->data, object->size,
		                        &object->view, (uint16_t)section.link,
		                        &object->strtab) != MT_ELF_OK ||
		    object->strtab.type != MT_SHT_STRTAB ||
		    section.entry_size < MT_ELF64_SYM_SIZE ||
		    section.size % section.entry_size != 0)
			return ld_errorf(ctx, "invalid symbol table", object->name);
		object->symtab_index = i;
		object->symtab = section;
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
		if (symbol->section >= object->view.section_count ||
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
	uint64_t j;
	if (prepare_object_symbol(ctx, object) != 0)
		return -1;
	if (!object->has_symtab)
		return 0;
	for (j = 0; j < object->symtab.size / object->symtab.entry_size; ++j) {
		if (mt_elf64_get_symbol(object->data, object->size,
		                        &object->symtab, j, &symbol) != MT_ELF_OK)
			return ld_errorf(ctx, "invalid symbol", object->name);
		if (symbol.name == 0 ||
		    mt_elf64_get_string(object->data, object->size,
		                       &object->strtab, symbol.name, &name) != MT_ELF_OK)
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

struct archive_extract_context {
	struct ld_context *ctx;
	const char *archive;
	int added;
};

static int
archive_member_needed(struct ld_context *ctx, const unsigned char *data,
                      size_t size)
{
	struct mt_elf64_view view;
	struct mt_elf64_section symtab;
	struct mt_elf64_section strtab;
	struct mt_elf64_symbol symbol;
	const char *name;
	uint16_t i;
	uint64_t j;
	unsigned binding;

	if (mt_elf64_parse(data, size, &view) != MT_ELF_OK ||
	    view.type != MT_ET_REL || view.machine != ctx->target->emachine)
		return 0;
	for (i = 0; i < view.section_count; ++i) {
		if (mt_elf64_get_section(data, size, &view, i, &symtab) != MT_ELF_OK)
			return -1;
		if (symtab.type != MT_SHT_SYMTAB)
			continue;
		if (symtab.link >= view.section_count ||
		    mt_elf64_get_section(data, size, &view, (uint16_t)symtab.link,
		                         &strtab) != MT_ELF_OK)
			return -1;
		for (j = 0; j < symtab.size / symtab.entry_size; ++j) {
			struct ld_global *global;
			if (mt_elf64_get_symbol(data, size, &symtab, j, &symbol) != MT_ELF_OK)
				return -1;
			binding = symbol.info >> LD_STB_SHIFT;
			if (binding == LD_STB_LOCAL || symbol.section == MT_SHN_UNDEF ||
			    symbol.name == 0 ||
			    mt_elf64_get_string(data, size, &strtab, symbol.name,
			                       &name) != MT_ELF_OK)
				continue;
			global = find_global(ctx, name);
			if (global && !global->defined)
				return 1;
		}
		break;
	}
	return 0;
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
	int needed = archive_member_needed(ctx, data, (size_t)member->size);
	if (needed < 0)
		return ld_errorf(ctx, "invalid archive member", member->name);
	if (!needed)
		return 0;
	snprintf(display, sizeof(display), "%s(%s)", extract->archive, member->name);
	index = ctx->objects.count;
	if (append_object(ctx, display, data, (size_t)member->size) != 0 ||
	    collect_one_object_sections(ctx, index) != 0 ||
	    collect_one_object_symbols(ctx, index) != 0)
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
			if (mt_ar_foreach(ctx->archives.paths[i], extract_archive_member,
			                  &extract) != 0)
				return ld_errorf(ctx, "cannot extract archive",
				                 ctx->archives.paths[i]);
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
	    mt_elf64_get_symbol(object->data, object->size, &object->symtab,
	                         index, symbol) != MT_ELF_OK)
		return ld_errorf(ctx, "relocation symbol is invalid", object->name);
	if (symbol->name == 0) {
		*name = "";
		return 0;
	}
	if (mt_elf64_get_string(object->data, object->size, &object->strtab,
	                        symbol->name, name) != MT_ELF_OK)
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
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined symbol", name);
		*value = ctx->groups[global->group].address + global->offset +
		         symbol.value;
		return 0;
	}
	if (symbol.section == LD_SHN_COMMON) {
		global = find_global(ctx, name);
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined common symbol", name);
		*value = ctx->groups[global->group].address + global->offset;
		return 0;
	}
	if (symbol.section >= object->view.section_count ||
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
	uint64_t info;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *object = &ctx->objects.items[i];
		for (j = 0; j < object->view.section_count; ++j) {
			if (mt_elf64_get_section(object->data, object->size,
			                        &object->view, (uint16_t)j, &section) != MT_ELF_OK)
				return -1;
			if (section.type != MT_SHT_RELA || section.entry_size < 24 ||
			    section.size % section.entry_size != 0)
				continue;
			for (n = 0; n < section.size / section.entry_size; ++n) {
				const unsigned char *p = object->data + section.offset + n * section.entry_size;
				info = read64(p + 8);
				if ((unsigned)info != LD_R_X86_64_GOTPCREL)
					continue;
				if (get_symbol_by_index(ctx, object, info >> 32, &symbol,
				                        &name) != 0 || add_got_entry(ctx, name) != 0)
					return -1;
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

static int
layout_output(struct ld_context *ctx)
{
	uint64_t offset = LD_PAGE;
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
			group->address = LD_BASE + offset;
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
			group->address = LD_BASE + offset;
			/* NOBITS: does not advance file offset */
		}
	}

	/* TLS sections: lay out .tdata (needs file space) then .tbss (NOBITS). */
	if (ctx->tls_tdata_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tdata_group];
		offset = align_up(offset, g->align ? g->align : 1);
		g->file_offset = offset;
		g->address = LD_BASE + offset;
		offset += g->size;
	}
	if (ctx->tls_tbss_group >= 0) {
		struct ld_group *g = &ctx->groups[ctx->tls_tbss_group];
		if (ctx->tls_tdata_group >= 0) {
			struct ld_group *td = &ctx->groups[ctx->tls_tdata_group];
			g->file_offset = td->file_offset + td->size;
			g->address = td->address + td->size;
		} else {
			/* No .tdata: .tbss shares address with .data for section header
			 * purposes, but does not consume PT_LOAD file or memory space. */
			int dg = find_group(ctx, ".data");
			if (dg >= 0) {
				g->file_offset = ctx->groups[dg].file_offset;
				g->address = ctx->groups[dg].address;
			} else {
				g->file_offset = offset;
				g->address = LD_BASE + offset;
			}
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
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined symbol", name);
		if (global->group == ctx->tls_tdata_group)
			*tls_offset = global->offset + symbol.value;
		else if (global->group == ctx->tls_tbss_group)
			*tls_offset = ctx->tls_tdata_size + global->offset + symbol.value;
		else
			return ld_errorf(ctx, "TPOFF32 relocation against non-TLS symbol", name);
		return 0;
	}
	if (symbol.section >= object->view.section_count ||
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
	uint64_t offset = read64(p + 0);
	uint64_t info = read64(p + 8);
	int type = (int)(info & 0xffffffffu);
	uint64_t symbol_index = info >> 32;
	int64_t addend = (int64_t)read64(p + 16);
	uint64_t resolved_value;
	const char *name;
	uint64_t place;
	uint64_t value;
	size_t got;
	struct ld_group *got_group;
	uint64_t target_offset;
	unsigned width;

	if (offset > target->size || target->size - offset <
	    (type == LD_R_X86_64_64 ? 8 : 4))
		return ld_error(ctx, "relocation offset is outside output section");
	if (symbol_value(ctx, object, symbol_index, &resolved_value, &name) != 0)
		return -1;
	target_offset = (uint64_t)object->maps[reloc_section->info].offset + offset;
	place = target->address + target_offset;
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
		for (j = 0; j < object->view.section_count; ++j) {
			if (mt_elf64_get_section(object->data, object->size,
			                        &object->view, j, &section) != MT_ELF_OK)
				return ld_errorf(ctx, "invalid relocation section", object->name);
			if (section.type != MT_SHT_RELA)
				continue;
			if (section.info >= object->view.section_count ||
			    object->maps[section.info].group < 0)
				return ld_error(ctx, "relocation target was discarded");
			group = object->maps[section.info].group;
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
		if (!global || !global->defined)
			return ld_errorf(ctx, "undefined GOT symbol", ctx->got.items[i].name);
		value = ctx->groups[global->group].address + global->offset;
		write64(got->data + ctx->got.items[i].offset, value);
	}
	return 0;
}

struct ld_output_section {
	const char *name;
	uint32_t type;
	uint64_t flags;
	uint64_t address;
	uint64_t offset;
	uint64_t size;
	uint64_t align;
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

static int
write_executable(struct ld_context *ctx, const char *path,
                 const char *entry, const struct mt_target *target)
{
	struct ld_group *entry_group = NULL;
	struct ld_global *entry_symbol;
	struct ld_strings shstr;
	struct ld_output_section *sections = NULL;
	uint32_t *name_offsets = NULL;
	FILE *file = NULL;
	uint64_t entry_address;
	uint64_t rx_end = LD_PAGE;
	uint64_t rw_start = UINT64_MAX;
	uint64_t file_end = LD_PAGE;
	uint64_t memory_end = LD_PAGE;
	uint64_t section_offset;
	uint64_t load_file_end;
	uint32_t shstr_index;
	size_t i;
	int output_count;
	int result = -1;

	entry_symbol = find_global(ctx, entry);
	if (!entry_symbol || !entry_symbol->defined)
		return ld_errorf(ctx, "entry symbol not found", entry);
	entry_group = &ctx->groups[entry_symbol->group];
	entry_address = entry_group->address + entry_symbol->offset;
	for (i = 0; i < ctx->group_count; ++i) {
		struct ld_group *group = &ctx->groups[i];
		if (is_tls_group(ctx, (int)i))
			continue;
		if (group->rank < 2)
			rx_end = group->type == MT_SHT_NOBITS ? rx_end :
			          group->file_offset + group->size > rx_end ?
			          group->file_offset + group->size : rx_end;
		else if (rw_start == UINT64_MAX || group->file_offset < rw_start)
			rw_start = group->file_offset;
		if (group->type != MT_SHT_NOBITS &&
		    file_end < group->file_offset + group->size)
			file_end = group->file_offset + group->size;
		if (memory_end < group->file_offset + group->size)
			memory_end = group->file_offset + group->size;
	}
	for (i = 0; i < ctx->group_count; ++i) {
		if (is_tls_group(ctx, (int)i))
			continue;
		if (ctx->groups[i].type == MT_SHT_NOBITS &&
		    memory_end < ctx->groups[i].file_offset + ctx->groups[i].size)
			memory_end = ctx->groups[i].file_offset + ctx->groups[i].size;
	}
	output_count = (int)ctx->group_count + 2; /* null + groups + shstrtab */
	sections = (struct ld_output_section *)calloc(output_count,
	                                               sizeof(*sections));
	name_offsets = (uint32_t *)calloc(output_count, sizeof(*name_offsets));
	if (!sections || !name_offsets)
		goto out;
	if (strings_init(&shstr) != 0)
		goto out;
	for (i = 0; i < ctx->group_count; ++i) {
		sections[i + 1] = (struct ld_output_section){
			.name = ctx->groups[i].name,
			.type = ctx->groups[i].type,
			.flags = ctx->groups[i].flags,
			.address = ctx->groups[i].address,
			.offset = ctx->groups[i].file_offset,
			.size = ctx->groups[i].size,
			.align = ctx->groups[i].align
		};
		if (strings_add(&shstr, sections[i + 1].name, &name_offsets[i + 1]) != 0)
			goto out_strings;
	}
	shstr_index = (uint32_t)(output_count - 1);
	load_file_end = file_end;
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
		write16(h + 16, 2);
		write16(h + 18, target->emachine);
		write32(h + 20, 1);
		write64(h + 24, entry_address);
		write64(h + 32, target->ehdr_size);
		write64(h + 40, section_offset);
		write32(h + 36, target->e_flags);
		write16(h + 52, target->ehdr_size);
		write16(h + 54, 56);
		write16(h + 56, ctx->tls_size ? 3 : 2);
		write16(h + 58, target->shdr_size);
		write16(h + 60, (uint16_t)output_count);
		write16(h + 62, (uint16_t)shstr_index);
		if (fwrite(h, 1, sizeof(h), file) != sizeof(h))
			goto out_file;
	}
	if (write_program_header(file, LD_PF_R | LD_PF_X, 0, LD_BASE,
	                        rx_end, rx_end) != 0)
		goto out_file;
	if (rw_start != UINT64_MAX &&
	    write_program_header(file, LD_PF_R | LD_PF_W, rw_start,
	                         LD_BASE + rw_start,
	                         load_file_end > rw_start ? load_file_end - rw_start : 0,
	                         memory_end - rw_start) != 0)
		goto out_file;
	if (ctx->tls_size) {
		uint64_t tls_addr = 0, tls_off = 0, tls_filesz = 0;
		if (ctx->tls_tdata_group >= 0) {
			tls_addr = ctx->groups[ctx->tls_tdata_group].address;
			tls_off = ctx->groups[ctx->tls_tdata_group].file_offset;
			tls_filesz = ctx->groups[ctx->tls_tdata_group].size;
		} else if (ctx->tls_tbss_group >= 0) {
			tls_addr = ctx->groups[ctx->tls_tbss_group].address;
			tls_off = ctx->groups[ctx->tls_tbss_group].file_offset;
		}
		if (write_program_header_type(file, LD_PT_TLS, LD_PF_R, tls_off,
		                               tls_addr, tls_filesz, ctx->tls_size,
		                               ctx->tls_align) != 0)
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
		write64(sh + 48, sections[i].align ? sections[i].align : 1);
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
	free(sections);
	free(name_offsets);
	return result;
}

int
mt_ld_link(const char *output, const char *entry,
           const char *const *inputs, size_t input_count,
           const char *target_name,
           const char **error_message)
{
	struct ld_context ctx;
	size_t i;
	int result = -1;
	memset(&ctx, 0, sizeof(ctx));
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
	ctx.got.group = -1;
	for (i = 0; i < input_count; ++i)
		if (load_input(&ctx, inputs[i]) != 0)
			goto out;
	if (ctx.objects.count == 0) {
		ld_error(&ctx, "no object files were loaded");
		goto out;
	}
	if (collect_sections(&ctx) != 0 || collect_symbols(&ctx) != 0 ||
	    extract_archives(&ctx) != 0 || allocate_common(&ctx) != 0 ||
	    collect_got_relocations(&ctx) != 0)
		goto out;
	if (ctx.got.count != 0)
		ctx.got.group = find_group(&ctx, ".got");
	if (layout_output(&ctx) != 0 || fill_got(&ctx) != 0 ||
	    apply_relocations(&ctx) != 0 ||
	    write_executable(&ctx, output, entry, ctx.target) != 0)
		goto out;
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
