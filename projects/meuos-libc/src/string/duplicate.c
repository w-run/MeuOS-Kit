#include <stdlib.h>
#include <string.h>

char *
strndup(const char *text, size_t limit)
{
	size_t length = strnlen(text, limit);
	char *copy = malloc(length + 1);
	if (!copy)
		return 0;
	memcpy(copy, text, length);
	copy[length] = 0;
	return copy;
}

char *
strdup(const char *text)
{
	return strndup(text, strlen(text));
}
