#include <stdlib.h>

int
main(void)
{
	return getenv("PATH") ? 0 : 1;
}
