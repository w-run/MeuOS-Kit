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
int
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
const char *
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

int
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

/* CIE deduplication for .eh_frame.
 * After all relocations have been applied, scan the .eh_frame section
 * and merge identical CIEs by comparing their full record content
 * (including length and CIE-id header).  All FDE CIE pointers are
 * adjusted to point to the retained (first) instance of each unique CIE.
 * Returns 0 on success, -1 on failure (ctx error already set). */
static int
dedup_eh_frame_cie(struct ld_context *ctx)
{
	int ehg = find_group(ctx, ".eh_frame");
	if (ehg < 0) return 0;
	struct ld_group *eh = &ctx->groups[ehg];
	if (eh->size == 0 || eh->type == MT_SHT_NOBITS) return 0;

	const unsigned char *data = eh->data;
	size_t size = eh->size;

	/* ---- First pass: collect CIE records ---- */
	struct cie_entry {
		size_t old_offset;
		size_t length;  /* content length (excl. 4-byte length field) */
	};
	struct cie_entry *cies = NULL;
	size_t cie_count = 0, cie_cap = 0;
	size_t offset = 0;

	while (offset + 8 <= size) {
		uint32_t len = read32(data + offset);
		if (len == 0) {
			offset += 4;  /* skip ZERO terminator, keep going */
			continue;
		}
		uint32_t end = offset + 4 + len;
		if (end > size) break;
		if (read32(data + offset + 4) == 0) {
			/* CIE record */
			if (cie_count == cie_cap) {
				size_t cap = cie_cap ? cie_cap * 2 : 16;
				struct cie_entry *tmp = (struct cie_entry *)
				    ld_realloc(cies, cap * sizeof(*tmp));
				if (!tmp) return ld_error(ctx, "out of memory");
				cies = tmp;
				cie_cap = cap;
			}
			cies[cie_count].old_offset = offset;
			cies[cie_count].length = len;
			cie_count++;
		}
		offset = end;
	}
	if (cie_count <= 1) {
		free(cies);
		return 0;  /* nothing to dedup */
	}

	/* ---- Second pass: identify duplicates ---- */
	int *cie_keep = (int *)ld_malloc(cie_count * sizeof(int));
	size_t *cie_new_offset = (size_t *)ld_malloc(cie_count * sizeof(size_t));
	if (!cie_keep || !cie_new_offset) {
		free(cies); free(cie_keep); free(cie_new_offset);
		return ld_error(ctx, "out of memory");
	}
	memset(cie_keep, 0, cie_count * sizeof(int));

	size_t deduped_cie_size = 0;
	cie_keep[0] = 1;
	cie_new_offset[0] = 0;
	deduped_cie_size += 4 + cies[0].length;

	for (size_t i = 1; i < cie_count; i++) {
		int dup = 0;
		size_t i_off = cies[i].old_offset;
		size_t i_len = cies[i].length;
		for (size_t j = 0; j < i; j++) {
			if (!cie_keep[j]) continue;
			size_t j_off = cies[j].old_offset;
			size_t j_len = cies[j].length;
			if (i_len == j_len &&
			    memcmp(data + i_off, data + j_off, 4 + i_len) == 0) {
				dup = 1;
				cie_new_offset[i] = cie_new_offset[j];
				break;
			}
		}
		if (!dup) {
			cie_keep[i] = 1;
			cie_new_offset[i] = deduped_cie_size;
			deduped_cie_size += 4 + i_len;
		}
	}

	/* ---- Measure total FDE payload ---- */
	size_t fde_total = 0;
	offset = 0;
	while (offset + 8 <= size) {
		uint32_t len = read32(data + offset);
		if (len == 0) {
			offset += 4;
			continue;
		}
		uint32_t end = offset + 4 + len;
		if (end > size) break;
		if (read32(data + offset + 4) != 0)
			fde_total += 4 + len;
		offset = end;
	}

	/* Always add a null terminator at the end */
	size_t new_size = deduped_cie_size + fde_total + 4;
	unsigned char *new_data = (unsigned char *)ld_malloc(new_size ? new_size : 1);
	if (!new_data) {
		free(cies); free(cie_keep); free(cie_new_offset);
		return ld_error(ctx, "out of memory");
	}

	/* ---- Third pass: rebuild ---- */
	size_t wp = 0;

	/* Write deduped CIEs */
	for (size_t i = 0; i < cie_count; i++) {
		if (!cie_keep[i]) continue;
		memcpy(new_data + wp, data + cies[i].old_offset, 4 + cies[i].length);
		wp += 4 + cies[i].length;
	}

	/* Write all FDEs with corrected CIE pointers */
	offset = 0;
	while (offset + 8 <= size) {
		uint32_t len = read32(data + offset);
		if (len == 0) {
			offset += 4;
			continue;
		}
		uint32_t end = offset + 4 + len;
		if (end > size) break;
		uint32_t id = read32(data + offset + 4);
		if (id != 0) {
			/* In .eh_frame, CIE_ptr = (int32_t)id is a signed offset:
			 *   id = CIE_addr - (FDE_field_addr)
			 * So: CIE_addr = FDE_field_addr + (int32_t)id */
			size_t cie_old = offset + 4 + (int32_t)id;
			size_t ci;
			for (ci = 0; ci < cie_count; ci++) {
				if (cies[ci].old_offset == cie_old)
					break;
			}
			if (ci >= cie_count) {
				/* Copy as-is (shouldn't happen) */
				memcpy(new_data + wp, data + offset, 4 + len);
				wp += 4 + len;
			} else {
				/* New CIE_ptr = cie_new_offset[ci] - (wp + 4) (signed) */
				uint32_t new_cie_ptr = (uint32_t)((int32_t)cie_new_offset[ci] - (int32_t)(wp + 4));
				memcpy(new_data + wp, data + offset, 4);         /* length */
				write32(new_data + wp + 4, new_cie_ptr);          /* fixed ptr */
				memcpy(new_data + wp + 8, data + offset + 8, len - 4); /* rest */
				wp += 4 + len;
			}
		}
		offset = end;
	}

	/* Write null terminator */
	write32(new_data + wp, 0);
	wp += 4;

	/* Replace group data */
	free(eh->data);
	eh->data = new_data;
	eh->size = wp;
	eh->capacity = wp;

	free(cies);
	free(cie_keep);
	free(cie_new_offset);
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
	/* Auto-define __init_array_start/__init_array_end/__fini_array_start/
	 * __fini_array_end as weak absolute symbols (value 0) so that libc's
	 * startup.c weak references don't cause "undefined symbol" errors.
	 * GNU ld defines these from section boundaries; defining them as
	 * weak absolutes preserves the existing behavior (empty array walk
	 * when the sections are absent) and satisfies the --no-undefined gate. */
	{
		static const char *crt_boundary_syms[] = {
			"_init",
			"_fini",
			"__preinit_array_start",
			"__preinit_array_end",
			"__init_array_start",
			"__init_array_end",
			"__fini_array_start",
			"__fini_array_end",
			NULL
		};
		for (int si = 0; crt_boundary_syms[si]; si++) {
			struct ld_global *g = get_global(&ctx, crt_boundary_syms[si]);
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
	    dedup_eh_frame_cie(&ctx) != 0 ||
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
