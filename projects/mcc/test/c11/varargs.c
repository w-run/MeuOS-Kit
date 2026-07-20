#include <stdarg.h>

static unsigned long
pick(const char *tag, ...)
{
	va_list arguments;
	int integer;
	double floating;
	unsigned long word;

	va_start(arguments, tag);
	integer = va_arg(arguments, int);
	floating = va_arg(arguments, double);
	word = va_arg(arguments, unsigned long);
	va_end(arguments);
	return integer == 7 && floating == 2.5 ? word : 0;
}

int
main(void)
{
	return pick("value", 7, 2.5, 9ul) == 9 ? 0 : 1;
}
