#include <stdbool.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <stdio.h>

_Alignas(16) static char storage;

int
main(void)
{
	bool value = true;
	if (!value || alignof(char) < 1 || sizeof(storage) != 1)
		return 1;
	puts("PASS C11 headers");
	return 0;
}
