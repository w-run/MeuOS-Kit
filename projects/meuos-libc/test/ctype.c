#include <ctype.h>
#include <stdio.h>

int
main(void)
{
	if (!isalpha('Z') || !isdigit('7') || !isxdigit('f') || !isspace('\n') ||
	    !isprint(' ') || tolower('Q') != 'q' || toupper('m') != 'M')
		return 1;
	puts("PASS ctype");
	return 0;
}
