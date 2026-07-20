#include <errno.h>

static _Thread_local int errno_value;

int *
__errno_location(void)
{
	return &errno_value;
}
