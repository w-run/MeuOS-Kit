#include <string.h>

static char *token_state;

static int
separator(int character, const char *delimiters)
{
	while (*delimiters)
		if (character == *delimiters++)
			return 1;
	return 0;
}

char *
strtok_r(char *text, const char *delimiters, char **state)
{
	char *begin;

	if (!text)
		text = *state;
	while (*text && separator(*text, delimiters))
		++text;
	if (!*text) {
		*state = text;
		return 0;
	}
	begin = text;
	while (*text && !separator(*text, delimiters))
		++text;
	if (*text)
		*text++ = 0;
	*state = text;
	return begin;
}

char *
strtok(char *text, const char *delimiters)
{
	return strtok_r(text, delimiters, &token_state);
}
