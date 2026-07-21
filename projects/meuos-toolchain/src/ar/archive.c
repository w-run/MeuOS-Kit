/* archive.c - small, reproducible SysV ar archive implementation. */
#include "mt/archive.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct ar_header {
	char bytes[MT_AR_HEADER_SIZE];
};

typedef int (*ar_visit_callback)(FILE *file, const struct mt_ar_member *member,
                                 void *context);

static void
set_error(enum mt_ar_status status)
{
	switch (status) {
	case MT_AR_E_ARGUMENT: errno = EINVAL; break;
	case MT_AR_E_FORMAT: errno = EINVAL; break;
	case MT_AR_E_NAME: errno = ENAMETOOLONG; break;
	case MT_AR_E_NOT_FOUND: errno = ENOENT; break;
	case MT_AR_E_IO: errno = EIO; break;
	default: break;
	}
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
	return *length <= MT_AR_MEMBER_NAME_MAX ? 0 : -1;
}

static int
write_member(FILE *archive, const char *path)
{
	const char *name;
	size_t name_length;
	struct stat st;
	struct ar_header header;
	FILE *input;
	unsigned char buffer[8192];
	size_t count;
	char pad = '\n';

	if (member_basename(path, &name, &name_length) != 0) {
		set_error(MT_AR_E_NAME);
		return -1;
	}
	if (stat(path, &st) != 0)
		return -1;
	if (st.st_size < 0 || (uintmax_t)st.st_size > UINT64_MAX) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	input = fopen(path, "rb");
	if (!input)
		return -1;

	memset(&header, ' ', sizeof(header));
	memcpy(header.bytes, name, name_length);
	if (name_length < MT_AR_MEMBER_NAME_SIZE)
		header.bytes[name_length] = '/';
	if (put_decimal(header.bytes + 16, 12, 0) != 0 ||
	    put_decimal(header.bytes + 28, 6, 0) != 0 ||
	    put_decimal(header.bytes + 34, 6, 0) != 0 ||
	    put_decimal(header.bytes + 40, 8, 0644) != 0 ||
	    put_decimal(header.bytes + 48, 10, (uint64_t)st.st_size) != 0) {
		fclose(input);
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	header.bytes[58] = '`';
	header.bytes[59] = '\n';
	if (write_all(archive, header.bytes, sizeof(header.bytes)) != 0)
		goto fail;
	while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
		if (write_all(archive, buffer, count) != 0)
			goto fail;
	}
	if (ferror(input))
		goto fail;
	if ((st.st_size & 1) != 0 && write_all(archive, &pad, 1) != 0)
		goto fail;
	fclose(input);
	return 0;

fail:
	fclose(input);
	return -1;
}

int
mt_ar_create(const char *archive_path, const char *const *members,
             size_t member_count)
{
	FILE *output;
	size_t i;

	if (!archive_path || (member_count != 0 && !members)) {
		set_error(MT_AR_E_ARGUMENT);
		return -1;
	}
	output = fopen(archive_path, "wb");
	if (!output)
		return -1;
	if (write_all(output, MT_AR_MAGIC, MT_AR_MAGIC_SIZE) != 0)
		goto fail;
	for (i = 0; i < member_count; ++i) {
		if (write_member(output, members[i]) != 0)
			goto fail;
	}
	if (fclose(output) != 0)
		return -1;
	return 0;

fail:
	fclose(output);
	return -1;
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

/* 返回 1 表示正常 EOF，0 表示成功，-1 表示格式或 I/O 错误。 */
static int
read_member_header(FILE *file, struct mt_ar_member *member, long *data_offset)
{
	struct ar_header header;
	char name[MT_AR_MEMBER_NAME_SIZE + 1];
	uint64_t size;
	size_t length;
	size_t i;
	int first;

	first = fgetc(file);
	if (first == EOF) {
		if (ferror(file))
			return -1;
		return 1;
	}
	ungetc(first, file);
	if (read_all(file, header.bytes, sizeof(header.bytes)) != 0)
		return -1;
	if (header.bytes[58] != '`' || header.bytes[59] != '\n') {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	length = 0;
	for (i = 0; i < MT_AR_MEMBER_NAME_SIZE && header.bytes[i] != ' '; ++i) {
		if (header.bytes[i] == '/')
			break;
		if (length == MT_AR_MEMBER_NAME_MAX) {
			set_error(MT_AR_E_NAME);
			return -1;
		}
		name[length++] = header.bytes[i];
	}
	if (length == 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	name[length] = '\0';
	if (parse_decimal(header.bytes + 48, 10, &size) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	if (size > (uint64_t)LONG_MAX) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	*data_offset = ftell(file);
	if (*data_offset < 0) {
		set_error(MT_AR_E_IO);
		return -1;
	}
	memset(member, 0, sizeof(*member));
	memcpy(member->name, name, length + 1);
	member->size = size;
	return 0;
}

static int
skip_member(FILE *file, long data_offset, uint64_t size)
{
	uint64_t next = (uint64_t)data_offset + size + (size & 1u);
	if (next > (uint64_t)LONG_MAX || fseek(file, (long)next, SEEK_SET) != 0) {
		set_error(MT_AR_E_FORMAT);
		return -1;
	}
	return 0;
}

static int
walk_archive(const char *archive_path, ar_visit_callback callback, void *context)
{
	FILE *file;
	char magic[MT_AR_MAGIC_SIZE];
	struct mt_ar_member member;
	long data_offset;
	int result;

	file = fopen(archive_path, "rb");
	if (!file)
		return -1;
	if (read_all(file, magic, sizeof(magic)) != 0 ||
	    memcmp(magic, MT_AR_MAGIC, sizeof(magic)) != 0) {
		set_error(MT_AR_E_FORMAT);
		fclose(file);
		return -1;
	}
	for (;;) {
		result = read_member_header(file, &member, &data_offset);
		if (result == 1)
			break;
		if (result != 0) {
			fclose(file);
			return -1;
		}
		result = callback(file, &member, context);
		if (result != 0) {
			fclose(file);
			return result > 0 ? 0 : -1;
		}
		if (skip_member(file, data_offset, member.size) != 0) {
			fclose(file);
			return -1;
		}
	}
	if (fclose(file) != 0)
		return -1;
	return 0;
}

struct list_context {
	mt_ar_member_callback callback;
	void *context;
};

static int
list_member(FILE *file, const struct mt_ar_member *member, void *context)
{
	struct list_context *list = (struct list_context *)context;
	(void)file;
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
copy_member(FILE *file, uint64_t size, FILE *output)
{
	unsigned char buffer[8192];
	size_t count;
	while (size != 0) {
		count = size < sizeof(buffer) ? (size_t)size : sizeof(buffer);
		if (fread(buffer, 1, count, file) != count ||
		    fwrite(buffer, 1, count, output) != count)
			return -1;
		size -= count;
	}
	return 0;
}

static int
print_member(FILE *file, const struct mt_ar_member *member, void *context)
{
	struct print_context *print = (struct print_context *)context;
	if (!name_selected(member->name, print->names, print->count))
		return 0;
	return copy_member(file, member->size, print->output);
}

int
mt_ar_print(const char *archive, const char *const *names, size_t name_count,
            FILE *output)
{
	/* 当前 callback API 保持简单；将 stdout 绑定改为临时 context，避免
	 * 对外暴露 FILE* 迭代器，同时保持实现可被 mcc 自举。 */
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
extract_member(FILE *file, const struct mt_ar_member *member, void *context)
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
	int copy_status = copy_member(file, member->size, output);
	int close_status = fclose(output);
	if (copy_status != 0 || close_status != 0)
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
	}
	return "unknown archive error";
}
