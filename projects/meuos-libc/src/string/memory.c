#include <stddef.h>
#include <string.h>

void *
memcpy(void *restrict destination, const void *restrict source, size_t count)
{
	unsigned char *out = destination;
	const unsigned char *in = source;
	void *result = destination;

	while (count--)
		*out++ = *in++;
	return result;
}

void *
memmove(void *destination, const void *source, size_t count)
{
	unsigned char *out = destination;
	const unsigned char *in = source;
	void *result = destination;

	if (out < in) {
		while (count--)
			*out++ = *in++;
	} else if (out > in) {
		out += count;
		in += count;
		while (count--)
			*--out = *--in;
	}
	return result;
}

void *
memset(void *destination, int value, size_t count)
{
	unsigned char *out = destination;
	void *result = destination;

	while (count--)
		*out++ = (unsigned char)value;
	return result;
}

int
memcmp(const void *left, const void *right, size_t count)
{
	const unsigned char *a = left;
	const unsigned char *b = right;

	while (count--) {
		if (*a != *b)
			return *a < *b ? -1 : 1;
		++a;
		++b;
	}
	return 0;
}

void *
memchr(const void *source, int value, size_t count)
{
	const unsigned char *in = source;
	unsigned char needle = (unsigned char)value;

	while (count--) {
		if (*in == needle)
			return (void *)in;
		++in;
	}
	return NULL;
}

size_t
strlen(const char *text)
{
	const char *end = text;
	while (*end)
		++end;
	return (size_t)(end - text);
}

size_t
strnlen(const char *text, size_t limit)
{
	size_t length = 0;
	while (length != limit && text[length])
		++length;
	return length;
}

char *
strcpy(char *restrict destination, const char *restrict source)
{
	char *result = destination;
	while ((*destination++ = *source++))
		;
	return result;
}

char *
strncpy(char *restrict destination, const char *restrict source, size_t count)
{
	char *result = destination;
	while (count) {
		char current = *source++;
		*destination++ = current;
		--count;
		if (!current)
			break;
	}
	while (count--) 
		*destination++ = '\0';
	return result;
}

char *
strcat(char *restrict destination, const char *restrict source)
{
	char *result = destination;

	while (*destination)
		++destination;
	while ((*destination++ = *source++))
		;
	return result;
}

char *
strncat(char *restrict destination, const char *restrict source, size_t count)
{
	char *result = destination;

	while (*destination)
		++destination;
	while (count && *source) {
		*destination++ = *source++;
		--count;
	}
	*destination = '\0';
	return result;
}

int
strcmp(const char *left, const char *right)
{
	while (*left && *left == *right) {
		++left;
		++right;
	}
	return (unsigned char)*left - (unsigned char)*right;
}

int
strncmp(const char *left, const char *right, size_t count)
{
	while (count && *left && *left == *right) {
		++left;
		++right;
		--count;
	}
	if (!count || (unsigned char)*left == (unsigned char)*right)
		return 0;
	return (unsigned char)*left < (unsigned char)*right ? -1 : 1;
}

char *
strchr(const char *text, int value)
{
	char needle = (char)value;
	while (*text) {
		if (*text == needle)
			return (char *)text;
		++text;
	}
	return needle == '\0' ? (char *)text : NULL;
}

char *
strrchr(const char *text, int value)
{
	const char *last = NULL;
	char needle = (char)value;

	do {
		if (*text == needle)
			last = text;
	} while (*text++);
	return (char *)last;
}

char *
strpbrk(const char *text, const char *accept)
{
	for (; *text; ++text)
		if (strchr(accept, (unsigned char)*text))
			return (char *)text;
	return 0;
}

char *
strstr(const char *text, const char *needle)
{
	const char *candidate;
	const char *match;
	const char *part;

	if (!*needle)
		return (char *)text;
	for (candidate = text; *candidate; ++candidate) {
		for (match = candidate, part = needle; *part && *match == *part;
			++match, ++part)
			;
		if (!*part)
			return (char *)candidate;
	}
	return 0;
}
