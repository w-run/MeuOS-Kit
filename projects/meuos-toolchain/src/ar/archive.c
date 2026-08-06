/* archive.c - reproducible SysV/GNU ar archive implementation. */
#include "mt/archive.h"
#include "mt/elf.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>

/* EOVERFLOW is not in POSIX.1-2008; provide fallback for strict mode builds. */
#ifndef EOVERFLOW
#define EOVERFLOW 75
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ar_blob {
	char *name;
	unsigned char *data;
	size_t size;
};

struct ar_collection {
	struct ar_blob *items;
	size_t count;
	size_t capacity;
};

struct ar_symbol {
	char *name;
	size_t member;
	uint64_t archive_offset;
};

struct ar_symbols {
	struct ar_symbol *items;
	size_t count;
	size_t capacity;
};

struct ar_layout {
	unsigned char *longnames;
	size_t long_size;
	size_t long_capacity;
	size_t *member_offsets;
	size_t *member_long_offsets;
};

struct ar_header {
	char bytes[MT_AR_HEADER_SIZE];
};

typedef int (*ar_visit_callback)(const struct mt_ar_member *member,
                                 const unsigned char *data, void *context);

static void
set_error(enum mt_ar_status status)
{
	switch (status) {
	case MT_AR_E_ARGUMENT: errno = EINVAL; break;
	case MT_AR_E_FORMAT: errno = EINVAL; break;
	case MT_AR_E_NAME: errno = ENAMETOOLONG; break;
	case MT_AR_E_NOT_FOUND: errno = ENOENT; break;
	case MT_AR_E_OVERFLOW: errno = EOVERFLOW; break;
	case MT_AR_E_IO: errno = EIO; break;
	default: break;
	}
}

static void *
mt_malloc(size_t size)
{
	void *p = malloc(size == 0 ? 1 : size);
	if (!p)
		errno = ENOMEM;
	return p;
}

static void *
mt_realloc(void *old, size_t size)
{
	void *p = realloc(old, size == 0 ? 1 : size);
	if (!p)
		errno = ENOMEM;
	return p;
}

static char *
mt_strdup(const char *value)
{
	size_t length;
	char *copy;

	if (!value)
		return NULL;
	length = strlen(value);
	copy = (char *)mt_malloc(length + 1);
	if (!copy)
		return NULL;
	memcpy(copy, value, length + 1);
	return copy;
}

static int
write_all(FILE *file, const void *data, size_t size)
{
	return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static int
read_all(FILE *file, void *data, size_t size)
{
	return fread(data, 1, size, file) == size ? 0 : -1;
}

static size_t
u64_decimal(char *out, uint64_t value)
{
	char reverse[32];
	size_t i = 0;
	size_t j;

	do {
		reverse[i++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	for (j = 0; j < i; ++j)
		out[j] = reverse[i - j - 1];
	return i;
}

static int
put_decimal(char *field, size_t width, uint64_t value)
{
	char digits[32];
	size_t length;

	memset(field, ' ', width);
	length = u64_decimal(digits, value);
	if (length > width)
		return -1;
	memcpy(field + width - length, digits, length);
	return 0;
}

static void
put_be32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)(value >> 24);
	p[1] = (unsigned char)(value >> 16);
	p[2] = (unsigned char)(value >> 8);
	p[3] = (unsigned char)value;
}

static int
parse_decimal(const char *field, size_t width, uint64_t *value)
{
	size_t i = 0;
	uint64_t result = 0;
	int saw_digit = 0;

	while (i < width && field[i] == ' ')
		++i;
	for (; i < width && field[i] != ' '; ++i) {
		unsigned digit;
		if (field[i] < '0' || field[i] > '9')
			return -1;
		digit = (unsigned)(field[i] - '0');
		if (result > (UINT64_MAX - digit) / 10)
			return -1;
		result = result * 10 + digit;
		saw_digit = 1;
	}
	while (i < width) {
		if (field[i] != ' ')
			return -1;
		++i;
	}
	if (!saw_digit)
		return -1;
	*value = result;
	return 0;
}

static int
parse_decimal_text(const char *text, size_t length, uint64_t *value)
{
	size_t i;
	uint64_t result = 0;

	if (length == 0)
		return -1;
	for (i = 0; i < length; ++i) {
		unsigned digit;
		if (text[i] < '0' || text[i] > '9')
			return -1;
		digit = (unsigned)(text[i] - '0');
		if (result > (UINT64_MAX - digit) / 10)
			return -1;
		result = result * 10 + digit;
	}
	*value = result;
	return 0;
}

static int
member_basename(const char *path, const char **name, size_t *length)
{
	const char *base;
	const char *slash;

	if (!path || !*path)
		return -1;
	base = path;
	slash = strrchr(path, '/');
	if (slash)
		base = slash + 1;
	if (!*base || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
		return -1;
	*name = base;
	*length = strlen(base);
	return 0;
}

static void
free_blob(struct ar_blob *blob)
{
	if (!blob)
		return;
	free(blob->name);
	free(blob->data);
	memset(blob, 0, sizeof(*blob));
}

static void
free_collection(struct ar_collection *collection)
{
	size_t i;
	if (!collection)
		return;
	for (i = 0; i < collection->count; ++i)
		free_blob(&collection->items[i]);
	free(collection->items);
	memset(collection, 0, sizeof(*collection));
}

static int
append_blob(struct ar_collection *collection, char *name,
            unsigned char *data, size_t size)
{
	struct ar_blob *items;
	size_t capacity;

	if (collection->count == collection->capacity) {
		capacity = collection->capacity ? collection->capacity * 2 : 16;
		if (capacity < collection->capacity ||
		    capacity > SIZE_MAX / sizeof(*items)) {
			set_error(MT_AR_E_OVERFLOW);
			return -1;
		}
		items = (struct ar_blob *)mt_realloc(collection->items,
		                                     capacity * sizeof(*items));
		if (!items)
			return -1;
		collection->items = items;
		collection->capacity = capacity;
	}
	collection->items[collection->count].name = name;
	collection->items[collection->count].data = data;
	collection->items[collection->count].size = size;
	++collection->count;
	return 0;
}

static int
read_file_blob(const char *path, struct ar_blob *blob)
{
	const char *name;
	size_t name_length;
	FILE *file;
	long length;
	unsigned char *data = NULL;
	char *copy = NULL;

	memset(blob, 0, sizeof(*blob));
	if (member_basename(path, &name, &name_length) != 0) {
		set_error(MT_AR_E_NAME);
		return -1;
	}
	file = fopen(path, "rb");
	if (!file)
		return -1;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
	    fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return -1;
	}
	if ((uint64_t)length > SIZE_MAX) {
		fclose(file);
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	data = (unsigned char *)mt_malloc((size_t)length);
	if (!data || (length != 0 && read_all(file, data, (size_t)length) != 0))
		goto fail;
	if (fclose(file) != 0)
		goto fail_no_file;
	copy = (char *)mt_malloc(name_length + 1);
	if (!copy)
		goto fail_no_file;
	memcpy(copy, name, name_length + 1);
	blob->name = copy;
	blob->data = data;
	blob->size = (size_t)length;
	return 0;

fail:
	fclose(file);
fail_no_file:
	free(data);
	return -1;
}

static int
read_member_data(FILE *file, uint64_t size, unsigned char **data)
{
	unsigned char *buffer;

	if (size > SIZE_MAX) {
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	buffer = (unsigned char *)mt_malloc((size_t)size);
	if (!buffer)
		return -1;
	if (size != 0 && read_all(file, buffer, (size_t)size) != 0) {
		free(buffer);
		return -1;
	}
	*data = buffer;
	return 0;
}

static int
skip_data(FILE *file, uint64_t size)
{
	int pad = (int)(size & 1u);
	if (size > (uint64_t)LONG_MAX - (uint64_t)pad ||
	    fseek(file, (long)size + pad, SEEK_CUR) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	return 0;
}

static int
read_header(FILE *file, char raw_name[MT_AR_MEMBER_NAME_SIZE + 1],
            uint64_t *size)
{
	struct ar_header header;
	char *end;
	int first;
	size_t i;

	first = fgetc(file);
	if (first == EOF) {
		if (ferror(file))
			return -1;
		return 1;
	}
	if (ungetc(first, file) == EOF ||
	    read_all(file, header.bytes, sizeof(header.bytes)) != 0)
		return -1;
	if (header.bytes[58] != '`' || header.bytes[59] != '\n') {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	memcpy(raw_name, header.bytes, MT_AR_MEMBER_NAME_SIZE);
	raw_name[MT_AR_MEMBER_NAME_SIZE] = '\0';
	end = raw_name + MT_AR_MEMBER_NAME_SIZE;
	while (end > raw_name && end[-1] == ' ')
		--end;
	*end = '\0';
	for (i = 0; i < MT_AR_MEMBER_NAME_SIZE && raw_name[i] != '\0'; ++i) {
		if ((unsigned char)raw_name[i] < 0x20 && raw_name[i] != '\t') {
			set_error(MT_AR_E_FORMAT);
			return -1;
		}
	}
	if (parse_decimal(header.bytes + 48, 10, size) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	return 0;
}

static int
resolve_long_name(const char *raw, const unsigned char *longnames,
                  size_t long_size, char **name)
{
	uint64_t offset;
	const unsigned char *start;
	const unsigned char *end;
	size_t raw_length;
	size_t length;
	char *copy;

	/* BSD #1/ extended name: filename stored at start of member data */
	if (raw[0] == '#' && raw[1] == '1' && raw[2] == '/') {
		uint64_t name_len;
		size_t num_len = 0;
		while (raw[3 + num_len] >= '0' && raw[3 + num_len] <= '9')
			++num_len;
		if (num_len == 0 ||
		    parse_decimal_text(raw + 3, num_len, &name_len) != 0)
			return -1;
		/* Signal BSD format: caller reads name from member data */
		*name = NULL;
		return (int)name_len;
	}

	if (raw[0] != '/') {
		raw_length = strlen(raw);
		if (raw_length > 0 && raw[raw_length - 1] == '/')
			--raw_length;
		copy = (char *)mt_malloc(raw_length + 1);
		if (!copy)
			return -1;
		memcpy(copy, raw, raw_length);
		copy[raw_length] = '\0';
		*name = copy;
		return 0;
	}
	if (raw[1] == '\0')
		return 1; /* symbol index */
	if (strcmp(raw, "//") == 0)
		return 2; /* long-name table */
	if (parse_decimal_text(raw + 1, strlen(raw + 1), &offset) != 0 ||
	    offset >= long_size) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	start = longnames + offset;
	end = longnames + long_size;
	length = 0;
	while (start + length < end && start[length] != '/' &&
	       start[length] != '\n' && start[length] != '\0')
		++length;
	if (start + length == end || length == 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	copy = (char *)mt_malloc(length + 1);
	if (!copy)
		return -1;
	memcpy(copy, start, length);
	copy[length] = '\0';
	*name = copy;
	return 0;
}

static int
load_archive(const char *path, struct ar_collection *collection)
{
	FILE *file;
	char magic[MT_AR_MAGIC_SIZE];
	char raw_name[MT_AR_MEMBER_NAME_SIZE + 1];
	unsigned char *longnames = NULL;
	uint64_t long_size = 0;
	uint64_t member_size;
	int result;

	memset(collection, 0, sizeof(*collection));
	file = fopen(path, "rb");
	if (!file)
		return -1;
	if (read_all(file, magic, sizeof(magic)) != 0 ||
	    memcmp(magic, MT_AR_MAGIC, sizeof(magic)) != 0) {
		set_error(MT_AR_E_FORMAT);
		goto fail;
	}
	for (;;) {
		unsigned char *data = NULL;
		char *name = NULL;
		int bsd_pad_consumed = 0;
		result = read_header(file, raw_name, &member_size);
		if (result == 1)
			break;
		if (result != 0)
			goto fail;
		result = resolve_long_name(raw_name, longnames, (size_t)long_size,
		                           &name);
		if (result < 0)
			goto fail;
		if (result == 2) {
			free(longnames);
			longnames = NULL;
			if (read_member_data(file, member_size, &longnames) != 0)
				goto fail;
			long_size = member_size;
			if ((member_size & 1u) != 0 && fgetc(file) != '\n') {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			continue;
		}
		if (result == 1) { /* symbol index */
			if (skip_data(file, member_size) != 0)
				goto fail;
			continue;
		}
		if (result >= 3) {
			/* BSD #1/ format: result is the filename length.
			 * Read full member data, then split into name + content. */
			size_t name_len = (size_t)result;
			if (name_len > member_size) {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			if (read_member_data(file, member_size, &data) != 0)
				goto fail;
			name = (char *)mt_malloc(name_len + 1);
			if (!name) {
				free(data);
				goto fail;
			}
			memcpy(name, data, name_len);
			name[name_len] = '\0';
			/* Shift data past the name prefix */
			{
				size_t content_size = (size_t)member_size - name_len;
				unsigned char *content = (unsigned char *)mt_malloc(content_size ? content_size : 1);
				if (!content) {
					free(name);
					free(data);
					goto fail;
				}
				memcpy(content, data + name_len, content_size);
				free(data);
				data = content;
				/* Consume the padding byte (if any) for the FULL member
				 * size (name+content); keep the odd content length intact.
				 * The outer pad check below is suppressed for BSD members
				 * since padding was handled here for both parities. */
				if (member_size & 1u) {
					if (fgetc(file) != '\n') {
						set_error(MT_AR_E_FORMAT);
						free(name); free(data); goto fail;
					}
				}
				bsd_pad_consumed = 1;
				member_size = content_size;
			}
		} else if (read_member_data(file, member_size, &data) != 0)
			goto fail;
		if (!bsd_pad_consumed && (member_size & 1u) && fgetc(file) != '\n') {
			set_error(MT_AR_E_FORMAT);
			free(name);
			free(data);
			goto fail;
		}
		if (append_blob(collection, name, data, (size_t)member_size) != 0) {
			free(name);
			free(data);
			goto fail;
		}
	}
	free(longnames);
	if (fclose(file) != 0) {
		free_collection(collection);
		return -1;
	}
	return 0;

fail:
	free(longnames);
	fclose(file);
	free_collection(collection);
	return -1;
}

static int
append_longname(struct ar_layout *layout, const char *name,
                size_t *offset)
{
	size_t length = strlen(name);
	size_t needed;
	unsigned char *buffer;

	if (length > SIZE_MAX - 2 ||
	    layout->long_size > SIZE_MAX - length - 2) {
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	needed = layout->long_size + length + 2;
	if (needed > layout->long_capacity) {
		size_t capacity = layout->long_capacity ? layout->long_capacity * 2 : 128;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2) {
				capacity = needed;
				break;
			}
			capacity *= 2;
		}
		buffer = (unsigned char *)mt_realloc(layout->longnames, capacity);
		if (!buffer)
			return -1;
		layout->longnames = buffer;
		layout->long_capacity = capacity;
	}
	*offset = layout->long_size;
	memcpy(layout->longnames + layout->long_size, name, length);
	layout->long_size += length;
	layout->longnames[layout->long_size++] = '/';
	layout->longnames[layout->long_size++] = '\n';
	return 0;
}

static void
free_symbols(struct ar_symbols *symbols)
{
	size_t i;
	for (i = 0; i < symbols->count; ++i)
		free(symbols->items[i].name);
	free(symbols->items);
	memset(symbols, 0, sizeof(*symbols));
}

static int
append_symbol(struct ar_symbols *symbols, const char *name, size_t member)
{
	struct ar_symbol *items;
	size_t capacity;
	char *copy;

	if (!name || !*name)
		return 0;
	copy = mt_strdup(name);
	if (!copy)
		return -1;
	if (symbols->count == symbols->capacity) {
		capacity = symbols->capacity ? symbols->capacity * 2 : 32;
		if (capacity < symbols->capacity ||
		    capacity > SIZE_MAX / sizeof(*items)) {
			free(copy);
			set_error(MT_AR_E_OVERFLOW);
			return -1;
		}
		items = (struct ar_symbol *)mt_realloc(symbols->items,
		                                       capacity * sizeof(*items));
		if (!items) {
			free(copy);
			return -1;
		}
		symbols->items = items;
		symbols->capacity = capacity;
	}
	symbols->items[symbols->count].name = copy;
	symbols->items[symbols->count].member = member;
	symbols->items[symbols->count].archive_offset = 0;
	++symbols->count;
	return 0;
}

static int
collect_blob_symbols(const struct ar_blob *blob, size_t member,
                     struct ar_symbols *symbols)
{
	struct mt_elf64_view view;
	struct mt_elf64_section table;
	struct mt_elf64_section strings;
	struct mt_elf64_symbol symbol;
	enum mt_elf_status status;
	const char *name;
	uint16_t section_index;
	uint64_t symbol_index;
	uint64_t symbol_count;
	unsigned binding;
	int found = 0;

	status = mt_elf64_parse(blob->data, blob->size, &view);
	if (status == MT_ELF_E_MAGIC)
		return 0; /* ar 可以归档任意非 ELF 文件。 */
	if (status != MT_ELF_OK)
		return 0; /* 非 ELF 或暂不支持的对象不贡献索引，但仍保留成员。 */
	for (section_index = 0; section_index < view.section_count;
	     ++section_index) {
		if (mt_elf64_get_section(blob->data, blob->size, &view, section_index,
		                         &table) != MT_ELF_OK)
			return -1;
		if (table.type != MT_SHT_SYMTAB && table.type != MT_SHT_DYNSYM)
			continue;
		if (table.link >= view.section_count ||
		    mt_elf64_get_section(blob->data, blob->size, &view,
		                         (uint16_t)table.link, &strings) != MT_ELF_OK ||
		    strings.type != MT_SHT_STRTAB ||
		    table.entry_size < MT_ELF64_SYM_SIZE ||
		    table.size % table.entry_size != 0)
			return -1;
		found = 1;
		symbol_count = table.size / table.entry_size;
		for (symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
			if (mt_elf64_get_symbol(blob->data, blob->size, &table,
			                        symbol_index, &symbol) != MT_ELF_OK)
				return -1;
			binding = (unsigned)(symbol.info >> MT_STB_SHIFT);
			if ((binding != MT_STB_GLOBAL && binding != MT_STB_WEAK &&
			     binding != MT_STB_GNU_UNIQUE) ||
			    symbol.section == MT_SHN_UNDEF || symbol.name == 0 ||
			    mt_elf64_get_string(blob->data, blob->size, &strings,
			                       symbol.name, &name) != MT_ELF_OK)
				continue;
			if (append_symbol(symbols, name, member) != 0)
				return -1;
		}
		if (found)
			break;
	}
	return 0;
}

static int
compare_symbols(const void *left, const void *right)
{
	const struct ar_symbol *a = (const struct ar_symbol *)left;
	const struct ar_symbol *b = (const struct ar_symbol *)right;
	int result = strcmp(a->name, b->name);
	if (result != 0)
		return result;
	if (a->member < b->member)
		return -1;
	if (a->member > b->member)
		return 1;
	return 0;
}

static int
build_layout(const struct ar_collection *collection, struct ar_layout *layout,
             struct ar_symbols *symbols, unsigned char **index_data,
             size_t *index_size)
{
	size_t i;
	size_t symbol_name_bytes = 0;
	size_t size;
	size_t offset = MT_AR_MAGIC_SIZE;
	unsigned char *index;

	memset(layout, 0, sizeof(*layout));
	if (collection->count > SIZE_MAX / sizeof(size_t)) {
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	layout->member_offsets = (size_t *)mt_malloc(
	    collection->count == 0 ? 1 : collection->count * sizeof(size_t));
	layout->member_long_offsets = (size_t *)mt_malloc(
	    collection->count == 0 ? 1 : collection->count * sizeof(size_t));
	if (!layout->member_offsets || !layout->member_long_offsets)
		return -1;
	for (i = 0; i < collection->count; ++i) {
		layout->member_long_offsets[i] = SIZE_MAX;
		if (strlen(collection->items[i].name) > MT_AR_MEMBER_NAME_MAX &&
		    append_longname(layout, collection->items[i].name,
		                    &layout->member_long_offsets[i]) != 0)
			return -1;
	}
	qsort(symbols->items, symbols->count, sizeof(symbols->items[0]),
	      compare_symbols);
	for (i = 0; i < symbols->count; ++i) {
		if (strlen(symbols->items[i].name) > SIZE_MAX - symbol_name_bytes - 1) {
			set_error(MT_AR_E_OVERFLOW);
			return -1;
		}
		symbol_name_bytes += strlen(symbols->items[i].name) + 1;
	}
	if (symbols->count > (SIZE_MAX - symbol_name_bytes - 4) / 4) {
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	if (symbols->count > UINT32_MAX) {
		set_error(MT_AR_E_OVERFLOW);
		return -1;
	}
	*index_size = 4 + symbols->count * 4 + symbol_name_bytes;
	index = (unsigned char *)mt_malloc(*index_size);
	if (!index)
		return -1;
	put_be32(index, (uint32_t)symbols->count);
	*index_data = index;

	if (*index_size > SIZE_MAX - MT_AR_HEADER_SIZE)
		goto overflow;
	offset += MT_AR_HEADER_SIZE + *index_size + (*index_size & 1u);
	if (layout->long_size != 0) {
		if (offset > SIZE_MAX - MT_AR_HEADER_SIZE - layout->long_size -
		                     (layout->long_size & 1u))
			goto overflow;
		offset += MT_AR_HEADER_SIZE + layout->long_size +
		          (layout->long_size & 1u);
	}
	for (i = 0; i < collection->count; ++i) {
		if (offset > UINT32_MAX) {
			set_error(MT_AR_E_OVERFLOW);
			return -1;
		}
		layout->member_offsets[i] = offset;
		if (collection->items[i].size > SIZE_MAX - MT_AR_HEADER_SIZE - 1)
			goto overflow;
		offset += MT_AR_HEADER_SIZE + collection->items[i].size +
		          (collection->items[i].size & 1u);
	}
	for (i = 0; i < symbols->count; ++i) {
		if (symbols->items[i].member >= collection->count ||
		    layout->member_offsets[symbols->items[i].member] > UINT32_MAX) {
			set_error(MT_AR_E_OVERFLOW);
			return -1;
		}
		symbols->items[i].archive_offset =
		    layout->member_offsets[symbols->items[i].member];
	}
	{
		size_t cursor = 4 + symbols->count * 4;
		for (i = 0; i < symbols->count; ++i) {
			put_be32(index + 4 + i * 4,
			         (uint32_t)symbols->items[i].archive_offset);
			size = strlen(symbols->items[i].name) + 1;
			memcpy(index + cursor, symbols->items[i].name, size);
			cursor += size;
		}
	}
	return 0;

overflow:
	free(index);
	set_error(MT_AR_E_OVERFLOW);
	return -1;
}

static int
write_header(FILE *file, const char *name, uint64_t size)
{
	struct ar_header header;
	size_t length;

	memset(&header, ' ', sizeof(header));
	length = strlen(name);
	if (length > MT_AR_MEMBER_NAME_SIZE)
		return -1;
	memcpy(header.bytes, name, length);
	if (put_decimal(header.bytes + 16, 12, 0) != 0 ||
	    put_decimal(header.bytes + 28, 6, 0) != 0 ||
	    put_decimal(header.bytes + 34, 6, 0) != 0 ||
	    put_decimal(header.bytes + 40, 8, 0644) != 0 ||
	    put_decimal(header.bytes + 48, 10, size) != 0)
		return -1;
	header.bytes[58] = '`';
	header.bytes[59] = '\n';
	return write_all(file, header.bytes, sizeof(header.bytes));
}

static int
write_record(FILE *file, const char *header_name, const unsigned char *data,
             size_t size)
{
	char pad = '\n';
	if (write_header(file, header_name, (uint64_t)size) != 0 ||
	    (size != 0 && write_all(file, data, size) != 0))
		return -1;
	if (size & 1u)
		return write_all(file, &pad, 1);
	return 0;
}

static int
write_archive(const char *path, const struct ar_collection *collection,
              struct ar_layout *layout, const unsigned char *index_data,
              size_t index_size)
{
	FILE *file;
	char header_name[32];
	size_t i;

	file = fopen(path, "wb");
	if (!file)
		return -1;
	if (write_all(file, MT_AR_MAGIC, MT_AR_MAGIC_SIZE) != 0)
		goto fail;
	if (write_record(file, "/", index_data, index_size) != 0)
		goto fail;
	if (layout->long_size != 0 &&
	    write_record(file, "//", layout->longnames, layout->long_size) != 0)
		goto fail;
	for (i = 0; i < collection->count; ++i) {
		if (strlen(collection->items[i].name) <= MT_AR_MEMBER_NAME_MAX) {
			memset(header_name, 0, sizeof(header_name));
			memcpy(header_name, collection->items[i].name,
			       strlen(collection->items[i].name));
			header_name[strlen(collection->items[i].name)] = '/';
		} else {
			size_t offset = layout->member_long_offsets[i];
			char digits[32];
			size_t digits_length = u64_decimal(digits, (uint64_t)offset);
			if (offset == SIZE_MAX || digits_length + 1 > 16)
				goto fail;
			memset(header_name, 0, sizeof(header_name));
			header_name[0] = '/';
			memcpy(header_name + 1, digits, digits_length);
		}
		if (write_record(file, header_name, collection->items[i].data,
		                 collection->items[i].size) != 0)
			goto fail;

	}
	if (fclose(file) != 0)
		return -1;
	return 0;

fail:
	fclose(file);
	return -1;
}

static int
prepare_archive(const struct ar_collection *collection,
                unsigned char **index_data, size_t *index_size,
                struct ar_layout *layout)
{
	struct ar_symbols symbols;
	size_t i;
	int result = -1;

	memset(&symbols, 0, sizeof(symbols));
	for (i = 0; i < collection->count; ++i) {
		if (collect_blob_symbols(&collection->items[i], i, &symbols) != 0)
			goto out;
	}
	result = build_layout(collection, layout, &symbols, index_data, index_size);
out:
	free_symbols(&symbols);
	return result;
}

static int
find_member(const struct ar_collection *collection, const char *name,
            size_t *index)
{
	size_t i;
	for (i = 0; i < collection->count; ++i) {
		if (strcmp(collection->items[i].name, name) == 0) {
			*index = i;
			return 0;
		}
	}
	return -1;
}

int
mt_ar_update(const char *archive, const char *const *members,
             size_t member_count, unsigned flags)
{
	struct ar_collection collection;
	struct ar_layout layout;
	unsigned char *index_data = NULL;
	size_t index_size = 0;
	size_t i;
	int exists;

	memset(&collection, 0, sizeof(collection));
	memset(&layout, 0, sizeof(layout));
	if (!archive || (member_count != 0 && !members) ||
	    ((flags & MT_AR_UPDATE_REPLACE) == 0 &&
	     (flags & MT_AR_UPDATE_APPEND) == 0)) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	{
		FILE *probe = fopen(archive, "rb");
		if (probe) {
			fclose(probe);
			exists = 1;
		} else {
			if (errno != ENOENT)
				return -1;
			exists = 0;
		}
	}
	if (exists) {
		if (load_archive(archive, &collection) != 0)
			return -1;
	} else {
		memset(&collection, 0, sizeof(collection));
	}
	for (i = 0; i < member_count; ++i) {
		struct ar_blob input;
		size_t old_index;
		if (read_file_blob(members[i], &input) != 0)
			goto fail;
		if ((flags & MT_AR_UPDATE_REPLACE) &&
		    find_member(&collection, input.name, &old_index) == 0) {
			free_blob(&collection.items[old_index]);
			collection.items[old_index] = input;
		} else if (append_blob(&collection, input.name, input.data,
		                       input.size) != 0) {
			free_blob(&input);
			goto fail;
		}
	}
	if (prepare_archive(&collection, &index_data, &index_size, &layout) != 0)
		goto fail_layout;
	if (write_archive(archive, &collection, &layout, index_data, index_size) != 0)
		goto fail_layout;
	free(index_data);
	free(layout.longnames);
	free(layout.member_offsets);
	free(layout.member_long_offsets);
	free_collection(&collection);
	return 0;

fail_layout:
	free(index_data);
	free(layout.longnames);
	free(layout.member_offsets);
	free(layout.member_long_offsets);
fail:
	free_collection(&collection);
	return -1;
}

int
mt_ar_create(const char *archive, const char *const *members,
             size_t member_count)
{
	return mt_ar_update(archive, members, member_count,
	                    MT_AR_UPDATE_REPLACE);
}

static int
walk_archive(const char *archive, ar_visit_callback callback, void *context)
{
	struct ar_collection collection;
	struct mt_ar_member member;
	size_t i;
	int result;

	if (load_archive(archive, &collection) != 0)
		return -1;
	for (i = 0; i < collection.count; ++i) {
		member.name = collection.items[i].name;
		member.size = collection.items[i].size;
		result = callback(&member, collection.items[i].data, context);
		if (result != 0) {
			free_collection(&collection);
			return result > 0 ? 0 : -1;
		}
	}
	free_collection(&collection);
	return 0;
}

struct list_context {
	mt_ar_member_callback callback;
	void *context;
};

static int
list_member(const struct mt_ar_member *member, const unsigned char *data,
            void *context)
{
	struct list_context *list = (struct list_context *)context;
	(void)data;
	return list->callback(member, list->context);
}

int
mt_ar_list(const char *archive, mt_ar_member_callback callback, void *context)
{
	struct list_context list;
	if (!archive || !callback) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	list.callback = callback;
	list.context = context;
	return walk_archive(archive, list_member, &list);
}

struct data_context {
	mt_ar_data_callback callback;
	void *context;
};

static int
data_member(const struct mt_ar_member *member, const unsigned char *data,
            void *context)
{
	struct data_context *visit = (struct data_context *)context;
	return visit->callback(member, data, visit->context);
}

int
mt_ar_foreach(const char *archive, mt_ar_data_callback callback, void *context)
{
	struct data_context visit;
	if (!archive || !callback) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	visit.callback = callback;
	visit.context = context;
	return walk_archive(archive, data_member, &visit);
}

/* ---- In-memory archive parsing (for VFS .msys) ---- */

/* Read header from memory buffer. Returns same semantics as read_header(). */
static int
mem_read_header(const unsigned char **pp, const unsigned char *end,
                char raw_name[MT_AR_MEMBER_NAME_SIZE + 1], uint64_t *size)
{
	const unsigned char *p = *pp;
	char *end_name;
	size_t i;

	if (end - p < MT_AR_HEADER_SIZE)
		return end == p ? 1 : -1;
	if (p[58] != '`' || p[59] != '\n') {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	memcpy(raw_name, p, MT_AR_MEMBER_NAME_SIZE);
	raw_name[MT_AR_MEMBER_NAME_SIZE] = '\0';
	end_name = raw_name + MT_AR_MEMBER_NAME_SIZE;
	while (end_name > raw_name && end_name[-1] == ' ')
		--end_name;
	*end_name = '\0';
	for (i = 0; i < MT_AR_MEMBER_NAME_SIZE && raw_name[i] != '\0'; ++i) {
		if ((unsigned char)raw_name[i] < 0x20 && raw_name[i] != '\t') {
			set_error(MT_AR_E_FORMAT);
			return -1;
		}
	}
	if (parse_decimal((const char *)p + 48, 10, size) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	*pp = p + MT_AR_HEADER_SIZE;
	return 0;
}

/* Load all archive members from a memory buffer into a collection. */
static int
load_archive_mem(const unsigned char *buf, size_t buf_size,
                 struct ar_collection *collection)
{
	const unsigned char *p = buf;
	const unsigned char *end = buf + buf_size;
	char raw_name[MT_AR_MEMBER_NAME_SIZE + 1];
	unsigned char *longnames = NULL;
	uint64_t long_size = 0;
	uint64_t member_size;
	int result;

	memset(collection, 0, sizeof(*collection));
	if (buf_size < MT_AR_MAGIC_SIZE ||
	    memcmp(p, MT_AR_MAGIC, MT_AR_MAGIC_SIZE) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	p += MT_AR_MAGIC_SIZE;

	for (;;) {
		unsigned char *data = NULL;
		char *name = NULL;

		result = mem_read_header(&p, end, raw_name, &member_size);
		if (result == 1)
			break;
		if (result != 0)
			goto fail;

		result = resolve_long_name(raw_name, longnames, (size_t)long_size, &name);
		if (result < 0)
			goto fail;

		if (result == 2) {
			/* Long-name table */
			free(longnames);
			longnames = NULL;
			if ((size_t)(end - p) < (size_t)member_size ||
			    member_size > SIZE_MAX) {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			longnames = (unsigned char *)mt_malloc((size_t)member_size);
			if (!longnames)
				goto fail;
			memcpy(longnames, p, (size_t)member_size);
			long_size = member_size;
			p += (size_t)member_size;
			if ((member_size & 1u) != 0) {
				if (p >= end || *p != '\n') {
					set_error(MT_AR_E_FORMAT);
					goto fail;
				}
				++p;
			}
			continue;
		}

		if (result == 1) {
			/* Symbol index: skip */
			size_t skip = (size_t)member_size;
			if ((size_t)(end - p) < skip) {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			p += skip;
			if ((member_size & 1u) != 0) {
				if (p >= end || *p != '\n') {
					set_error(MT_AR_E_FORMAT);
					goto fail;
				}
				++p;
			}
			continue;
		}

		if (result >= 3) {
			/* BSD #1/ format */
			size_t name_len = (size_t)result;
			if (name_len > member_size ||
			    (size_t)(end - p) < (size_t)member_size) {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			data = (unsigned char *)mt_malloc((size_t)member_size);
			if (!data)
				goto fail;
			memcpy(data, p, (size_t)member_size);
			name = (char *)mt_malloc(name_len + 1);
			if (!name) {
				free(data);
				goto fail;
			}
			memcpy(name, data, name_len);
			name[name_len] = '\0';
			{
				size_t content_size = (size_t)member_size - name_len;
				unsigned char *content = (unsigned char *)
					mt_malloc(content_size ? content_size : 1);
				if (!content) {
					free(name);
					free(data);
					goto fail;
				}
				memcpy(content, data + name_len, content_size);
				free(data);
				data = content;
				member_size = content_size & ~1u;
			}
			p += (size_t)member_size + name_len;
			/* BSD padding already includes the name; adjust */
			if ((member_size + name_len) & 1u) {
				if (p >= end || *p != '\n') {
					set_error(MT_AR_E_FORMAT);
					free(name); free(data); goto fail;
				}
				++p;
			}
		} else {
			/* Regular member */
			if ((size_t)(end - p) < (size_t)member_size) {
				set_error(MT_AR_E_FORMAT);
				goto fail;
			}
			data = (unsigned char *)mt_malloc((size_t)member_size);
			if (!data)
				goto fail;
			memcpy(data, p, (size_t)member_size);
			p += (size_t)member_size;
			if ((member_size & 1u) != 0) {
				if (p >= end || *p != '\n') {
					set_error(MT_AR_E_FORMAT);
					free(name); free(data);
					goto fail;
				}
				++p;
			}
		}

		if (append_blob(collection, name, data, (size_t)member_size) != 0) {
			free(name);
			free(data);
			goto fail;
		}
	}

	free(longnames);
	return 0;

fail:
	free(longnames);
	free_collection(collection);
	return -1;
}

int
mt_ar_foreach_mem(const unsigned char *data, size_t size,
                  mt_ar_data_callback callback, void *context)
{
	struct ar_collection collection;
	struct mt_ar_member member;
	struct data_context visit;
	size_t i;
	int result;

	if (!data || !callback) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	if (load_archive_mem(data, size, &collection) != 0)
		return -1;
	visit.callback = callback;
	visit.context = context;
	for (i = 0; i < collection.count; ++i) {
		member.name = collection.items[i].name;
		member.size = collection.items[i].size;
		result = data_member(&member, collection.items[i].data, &visit);
		if (result != 0) {
			free_collection(&collection);
			return result > 0 ? 0 : -1;
		}
	}
	free_collection(&collection);
	return 0;
}

struct names_context {
	const char *const *names;
	size_t count;
};

struct print_context {
	const char *const *names;
	size_t count;
	FILE *output;
};

static int
name_selected(const char *name, const char *const *names, size_t count)
{
	size_t i;
	if (count == 0)
		return 1;
	for (i = 0; i < count; ++i) {
		if (strcmp(name, names[i]) == 0)
			return 1;
	}
	return 0;
}

static int
copy_member(const unsigned char *data, size_t size, FILE *output)
{
	return size == 0 || fwrite(data, 1, size, output) == size ? 0 : -1;
}

static int
print_member(const struct mt_ar_member *member, const unsigned char *data,
             void *context)
{
	struct print_context *print = (struct print_context *)context;
	if (!name_selected(member->name, print->names, print->count))
		return 0;
	return copy_member(data, (size_t)member->size, print->output);
}

int
mt_ar_print(const char *archive, const char *const *names, size_t name_count,
            FILE *output)
{
	struct print_context selected;
	if (!archive || !output || (name_count != 0 && !names)) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	selected.names = names;
	selected.count = name_count;
	selected.output = output;
	return walk_archive(archive, print_member, &selected);
}

static int
safe_extract_name(const char *name)
{
	return name[0] != '/' && strstr(name, "..") == NULL &&
	       strchr(name, '\\') == NULL;
}

static int
extract_member(const struct mt_ar_member *member, const unsigned char *data,
               void *context)
{
	struct names_context *names = (struct names_context *)context;
	FILE *output;
	if (!name_selected(member->name, names->names, names->count))
		return 0;
	if (!safe_extract_name(member->name)) {
		set_error(MT_AR_E_NAME);
		return -1;
	}
	output = fopen(member->name, "wb");
	if (!output)
		return -1;
	if (copy_member(data, (size_t)member->size, output) != 0 ||
	    fclose(output) != 0)
		return -1;
	return 0;
}

int
mt_ar_extract(const char *archive, const char *const *names, size_t name_count)
{
	struct names_context selected;
	if (!archive || (name_count != 0 && !names)) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	selected.names = names;
	selected.count = name_count;
	return walk_archive(archive, extract_member, &selected);
}

const char *
mt_ar_status_string(enum mt_ar_status status)
{
	switch (status) {
	case MT_AR_OK: return "ok";
	case MT_AR_E_ARGUMENT: return "invalid argument";
	case MT_AR_E_IO: return "I/O error";
	case MT_AR_E_FORMAT: return "invalid archive format";
	case MT_AR_E_NAME: return "unsupported member name";
	case MT_AR_E_NOT_FOUND: return "member not found";
	case MT_AR_E_OVERFLOW: return "archive value overflow";
	}
	return "unknown archive error";
}
