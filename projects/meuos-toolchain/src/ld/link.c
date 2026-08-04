/* link.c - static ET_REL -> ET_EXEC linker. */
#include "ld_internal.h"



void *
ld_malloc(size_t size)
{
	void *p = malloc(size == 0 ? 1 : size);
	return p;
}

void *
ld_realloc(void *old, size_t size)
{
	return realloc(old, size == 0 ? 1 : size);
}

char *
ld_strdup(const char *text)
{
	size_t n = strlen(text);
	char *copy = (char *)ld_malloc(n + 1);
	if (copy)
		memcpy(copy, text, n + 1);
	return copy;
}

int
ld_error(struct ld_context *ctx, const char *message)
{
	strncpy(ctx->error, message, sizeof(ctx->error) - 1);
	ctx->error[sizeof(ctx->error) - 1] = '\0';
	return -1;
}

int
ld_errorf(struct ld_context *ctx, const char *prefix, const char *name)
{
	if (name)
		snprintf(ctx->error, sizeof(ctx->error), "%s: %s", prefix, name);
	else
		ld_error(ctx, prefix);
	return -1;
}

uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	       (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t
read16(const unsigned char *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

uint64_t
read64(const unsigned char *p)
{
	return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}

void
write16(unsigned char *p, uint16_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
}

void
write32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
	p[2] = (unsigned char)(value >> 16);
	p[3] = (unsigned char)(value >> 24);
}

void
write64(unsigned char *p, uint64_t value)
{
	unsigned i;
	for (i = 0; i < 8; ++i)
		p[i] = (unsigned char)(value >> (i * 8));
}

uint64_t
align_up(uint64_t value, uint64_t align)
{
	uint64_t mask = align - 1;
	return (value + mask) & ~mask;
}

/* ---- ELF32/64 dispatch helpers ---- */

/* Parse an object file's ELF header.  Returns 0 on success, -1 on failure. */
static int
object_parse(struct ld_context *ctx, struct ld_object *object)
{
	enum mt_elf_status status;
	if (ctx->target->elf_class == 1) {
		status = mt_elf32_parse(object->data, object->size, &object->view.v32);
		if (status != MT_ELF_OK ||
		    (object->view.v32.type != MT_ET_REL &&
		     object->view.v32.type != MT_ET_DYN) ||
		    object->view.v32.machine != ctx->target->emachine)
			return -1;
		object->is_shared = (object->view.v32.type == MT_ET_DYN);
	} else {
		status = mt_elf64_parse(object->data, object->size, &object->view.v64);
		if (status != MT_ELF_OK ||
		    (object->view.v64.type != MT_ET_REL &&
		     object->view.v64.type != MT_ET_DYN) ||
		    object->view.v64.machine != ctx->target->emachine)
			return -1;
		object->is_shared = (object->view.v64.type == MT_ET_DYN);
	}
	object->elf_class = ctx->target->elf_class;
	return 0;
}

/* Section count accessor — works for both 32 and 64 bit views. */
uint16_t
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
int
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
			for (j = 0; s32.entry_size > 0 && j < s32.size / s32.entry_size; ++j) {
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
			for (j = 0; s64.entry_size > 0 && j < s64.size / s64.entry_size; ++j) {
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
	if (ctx->tls_descs) {
		size_t di;
		for (di = 0; di < ctx->tls_desc_count; ++di)
			free(ctx->tls_descs[di].name);
		free(ctx->tls_descs);
	}
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
		/* Non-VFS .a file: still need to align archive_mem_data index so that
		 * extract_archives can use archive_mem_count to identify VFS archives.
		 * Non-VFS archives get a NULL entry. */
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
		ctx->archive_mem_data[ctx->archive_mem_count] = NULL;
		ctx->archive_mem_size[ctx->archive_mem_count] = 0;
		ctx->archive_mem_count++;
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
	if (strcmp(name, ".plt") == 0) return 0;  /* executable, like .text */
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
	if (strcmp(name, ".init_array") == 0) return 3;
	if (strcmp(name, ".fini_array") == 0) return 3;
	if (strcmp(name, ".preinit_array") == 0) return 3;
	if (strcmp(name, ".bss") == 0 || strcmp(name, ".tbss") == 0) return 4;
	/* Debug and other non-allocatable sections go after ALL loaded sections */
	if (strncmp(name, ".debug", 6) == 0) return 5;
	if (strncmp(name, ".note", 5) == 0) return 5;
	if (strncmp(name, ".comment", 8) == 0) return 5;
	return 6;
}

int
is_tls_group(struct ld_context *ctx, int group)
{
	return group == ctx->tls_tdata_group || group == ctx->tls_tbss_group;
}

int
find_group(struct ld_context *ctx, const char *name)
{
	size_t i;
	for (i = 0; i < ctx->group_count; ++i)
		if (strcmp(ctx->groups[i].name, name) == 0)
			return (int)i;
	return -1;
}

int
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

int
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
		if (object->is_shared)
			continue;
		for (j = 0; j < object_section_count(object); ++j) {
			if (object_get_section(object, j, &section) != 0)
			return ld_errorf(ctx, "invalid section in object", object->name);
		if (section.type != MT_SHT_PROGBITS &&
		    section.type != MT_SHT_NOBITS &&
		    section.type != MT_SHT_INIT_ARRAY &&
		    section.type != MT_SHT_FINI_ARRAY &&
		    section.type != MT_SHT_PREINIT_ARRAY)
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
	if (object->is_shared)
		return 0;
	for (j = 0; j < object_section_count(object); ++j) {
		if (object_get_section(object, j, &section) != 0)
			return ld_errorf(ctx, "invalid section in object", object->name);
		if (section.type != MT_SHT_PROGBITS && section.type != MT_SHT_NOBITS &&
		    section.type != MT_SHT_INIT_ARRAY &&
		    section.type != MT_SHT_FINI_ARRAY &&
		    section.type != MT_SHT_PREINIT_ARRAY)
			continue;
		name = object_section_name(object, j);
		if (!name || !*name)
			return ld_errorf(ctx, "section has no name", object->name);
		group = get_group(ctx, name, section.type, section.flags,
		                  section.alignment ? section.alignment : 1);
		if (group < 0)
			return ld_error(ctx, "out of memory");
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

struct ld_global *
find_global(struct ld_context *ctx, const char *name)
{
	size_t i;
	for (i = 0; i < ctx->globals.count; ++i)
		if (strcmp(ctx->globals.items[i].name, name) == 0)
			return &ctx->globals.items[i];
	return NULL;
}

struct ld_global *
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
	if (object->is_shared)
		return 0;
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

/* Extract exported symbols from a shared library (.so) object.
 * .so files have SHT_DYNSYM (not SHT_SYMTAB) for exported symbols.
 * Symbols are added to globals as "defined" to satisfy references
 * from other objects, but without a group mapping (.so sections
 * are not merged into the output). */
static int
collect_shared_object_symbols(struct ld_context *ctx, size_t object_index)
{
	struct ld_object *object = &ctx->objects.items[object_index];
	struct mt_elf64_section section;
	uint16_t j;
	int dso_dynsym_sec = -1, dso_dynstr_sec = -1;

	for (j = 0; j < object_section_count(object); ++j) {
		if (object_get_section(object, j, &section) != 0) continue;
		if (section.type == MT_SHT_DYNSYM) {
			dso_dynsym_sec = j;
			if (section.link < object_section_count(object))
				dso_dynstr_sec = (int)section.link;
			break;
		}
	}
	if (dso_dynsym_sec < 0) return 0;

	size_t entry_size = (size_t)section.entry_size;
	size_t sym_size = (object->elf_class == 1) ? 16 : 24;
	if (entry_size < sym_size || section.size < entry_size) return 0;
	size_t sym_count = section.size / entry_size;

	for (size_t si = 1; si < sym_count; si++) {
		struct mt_elf64_section dynsym_sec;
		struct mt_elf64_symbol sym;
		if (object_get_section(object, (uint16_t)dso_dynsym_sec,
		                       &dynsym_sec) != 0)
			continue;
		if (mt_elf64_get_symbol(object->data, object->size,
		                        &dynsym_sec, si, &sym) != MT_ELF_OK)
			continue;
		if (sym.section == 0 || sym.name == 0) continue;
		int binding = (sym.info >> 4) & 0xf;
		if (binding == 0) continue;

		/* Get name from .dynstr */
		const char *symname = NULL;
		if (dso_dynstr_sec >= 0) {
			struct mt_elf64_section ds;
			if (object_get_section(object, (uint16_t)dso_dynstr_sec, &ds) == 0) {
				if (mt_elf64_get_string(object->data, object->size,
				                        &ds, sym.name, &symname) != MT_ELF_OK)
					symname = NULL;
			}
		}
		if (!symname || !*symname) continue;

		struct ld_global *g = get_global(ctx, symname);
		if (!g) return ld_error(ctx, "out of memory");
		if (!g->defined) {
			g->defined = 1;
			g->weak = (binding == 2);
			g->offset = sym.value;
			g->size = sym.size;
			g->group = -1;
		}
	}
	return 0;
}

static int
collect_symbols(struct ld_context *ctx)
{
	size_t i;
	for (i = 0; i < ctx->objects.count; ++i) {
		struct ld_object *obj = &ctx->objects.items[i];
		if (obj->is_shared) {
			if (collect_shared_object_symbols(ctx, i) != 0)
				return -1;
		} else {
			if (collect_one_object_symbols(ctx, i) != 0)
				return -1;
		}
	}
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

int
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

int
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
	/* Skip nameless undefined symbols (symbol index 0 in RISCV,
	 * relaxation markers with no associated symbol name). */
	if (symbol.section == MT_SHN_UNDEF && (!name || !name[0])) {
		if (value) *value = 0;
		return 0;
	}
	if (symbol.section == MT_SHN_UNDEF) {
		global = find_global(ctx, name);
		if (!global)
			return ld_errorf(ctx, "undefined symbol", name);
		/* Follow alias chain (from --wrap) */
		while (global->alias)
			global = global->alias;
		if (!global->defined) {
			/* For shared/PIE output (ET_DYN), undefined external
			 * symbols are resolved at load time by ld.so.  Return 0
			 * as the placeholder value; the PLT32/GOTPCREL relocation
			 * handler will allocate a GOT entry and emit a JUMP_SLOT /
			 * GLOB_DAT dynamic relocation so ld.so fills the correct
			 * address. */
			if (ctx->shared || ctx->pie) {
				*value = 0;
				return 0;
			}
			return ld_errorf(ctx, "undefined symbol", name);
		}
		if (global->absolute)
			*value = global->offset + symbol.value;
		else if (global->group < 0) {
			/* Symbol defined only in an input shared library (its
			 * global has no output-section group).  It is external to
			 * this ET_DYN and must be resolved by ld.so at load time,
			 * so return 0 and let the PLT32/GOTPCREL dynamic-relocation
			 * path emit the GLOB_DAT/JUMP_SLOT entry.  Returning a
			 * baked-in vaddr from the .so (groups[-1] junk) would leave
			 * a static self-reference that crashes at runtime. */
			if (ctx->shared || ctx->pie) {
				*value = 0;
				return 0;
			}
			*value = 0;
		} else
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
		ctx.cref = opts->cref;
		ctx.link_script = opts->link_script;
		ctx.dynamic_linker = opts->dynamic_linker;
		ctx.add_needed = opts->add_needed;
		ctx.add_needed_count = opts->add_needed_count;
		/* Parse --version-script file into symbol list */
		ctx.version_script = NULL;
		ctx.version_script_count = 0;
		if (opts->version_script) {
			/* Simple parser: read file, find { global: ... ; local: * ; }
			 * and extract global symbol names. */
			FILE *vf = fopen(opts->version_script, "r");
			if (!vf) {
				ld_error(&ctx, "cannot open version script");
				goto out;
			}
			char line[4096];
			/* Allocate a large symbol array (up to 1024 entries) */
			const char **vs = (const char **)ld_malloc(1024 * sizeof(const char *));
			size_t vs_count = 0;
			while (fgets(line, sizeof(line), vf)) {
				char *p = line;
				while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '{' || *p == '}')
					p++;
				if (strncmp(p, "global:", 7) == 0) {
					p += 7;
					/* Parse semicolon-separated symbols */
					while (*p) {
						while (*p == ' ' || *p == '\t') p++;
						if (*p == ';' || *p == '}' || *p == '\0') {
							if (*p == ';') p++;
							break;
						}
						/* Extract symbol name up to ; or space */
						const char *start = p;
						while (*p && *p != ';' && *p != ' ' && *p != '\t' && *p != '}') p++;
						if (p > start && *start != '*' && vs_count < 1024) {
							size_t nlen = (size_t)(p - start);
							char *sym = (char *)ld_malloc(nlen + 1);
							if (sym) {
								memcpy(sym, start, nlen);
								sym[nlen] = '\0';
								vs[vs_count++] = sym;
							}
						}
					}
				}
			}
			fclose(vf);
			ctx.version_script = vs;
			ctx.version_script_count = vs_count;
		}
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
	    collect_got_relocations(&ctx) != 0 ||
	    collect_tls_descriptors(&ctx) != 0)
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
	/* Architecture-specific auto-defined symbols (--defsym equivalents for
	 * symbols that libgcc/crt expects but may not be provided by object files).
	 * These are defined as absolute symbols with value 0 so they satisfy
	 * undefined references without patching actual code. */
	if (strcmp(ctx.target->name, "riscv64") == 0) {
		/* __global_pointer$: RISC-V gp-relative small data access anchor.
		 * GCC emits gp-relative relocations that reference this symbol;
		 * defining it to 0 prevents "undefined reference" errors. */
		struct ld_global *gp = get_global(&ctx, "__global_pointer$");
		if (gp && !gp->defined) {
			gp->defined = 1;
			gp->offset = 0;
			gp->absolute = 1;
			gp->weak = 1;
			gp->common = 0;
			gp->group = -1;
		}
	}
	if (strcmp(ctx.target->name, "aarch64") == 0) {
		/* __aarch64_swp*: GCC libgcc atomic operation helpers.
		 * libc's malloc.o and state.o reference these functions; define
		 * them as absolute weak symbols so linking succeeds. */
		static const char *aarch64_swp_syms[] = {
			"__aarch64_swp1_acq",
			"__aarch64_swp4_acq_rel",
			NULL
		};
		for (int si = 0; aarch64_swp_syms[si]; si++) {
			struct ld_global *g = get_global(&ctx, aarch64_swp_syms[si]);
			if (g && !g->defined) {
				g->defined = 1;
				g->offset = 0;
				g->absolute = 1;
				g->weak = 1;
				g->common = 0;
				g->group = -1;
			}
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
				const char *uname = ctx.globals.items[ui].name;
				/* Build combined message with suggestion */
				char msg[512];
				snprintf(msg, sizeof(msg), "undefined reference to '%s'", uname);
				/* Suggest close matches from defined symbols */
				size_t namelen = strlen(uname);
				int best_dist = 8, best_idx = -1;
				for (size_t si = 0; si < ctx.globals.count; si++) {
					if (!ctx.globals.items[si].defined ||
					    !ctx.globals.items[si].name) continue;
					const char *cname = ctx.globals.items[si].name;
					if (strlen(cname) < namelen - 3 ||
					    strlen(cname) > namelen + 3) continue;
					int dist = 0;
					for (const char *a = uname, *b = cname; *a || *b; a++, b++) {
						if (!*a || !*b) { dist += 3; continue; }
						if (*a != *b) dist++;
					}
					if (dist < best_dist) { best_dist = dist; best_idx = (int)si; }
				}
				if (best_idx >= 0) {
					size_t mlen = strlen(msg);
					snprintf(msg + mlen, sizeof(msg) - mlen,
					         " (did you mean '%s'?)",
					         ctx.globals.items[best_idx].name);
				}
				ld_error(&ctx, msg);
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
	/* --cref: output cross-reference table */
	if (ctx.cref) {
		fprintf(stderr, "\nCross-reference table:\n");
		fprintf(stderr, "%-30s %-20s %-10s %s\n", "Symbol", "Defined in", "Section", "Value");
		size_t gi;
		for (gi = 0; gi < ctx.globals.count; gi++) {
			struct ld_global *g = &ctx.globals.items[gi];
			if (!g->defined) continue;
			const char *obj_name = g->object ? g->object->name : "(builtin)";
			const char *sec_name = "";
			uint64_t val = g->offset;
			if (g->group >= 0 && (size_t)g->group < ctx.group_count) {
				sec_name = ctx.groups[g->group].name;
				val = ctx.groups[g->group].address + g->offset;
			}
			fprintf(stderr, "%-30s %-20s %-10s 0x%08llx\n",
			        g->name, obj_name, sec_name,
			        (unsigned long long)val);
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
