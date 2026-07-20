#include <stdlib.h>
#include <string.h>

extern char **environ;

char *
getenv(const char *name)
{
	size_t length;
	char **entry;

	if (!name || !*name || !environ)
		return 0;
	length = strlen(name);
	for (entry = environ; *entry; ++entry)
		if (strncmp(*entry, name, length) == 0 && (*entry)[length] == '=')
			return *entry + length + 1;
	return 0;
}
