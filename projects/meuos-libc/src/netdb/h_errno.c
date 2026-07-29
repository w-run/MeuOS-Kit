#include <netdb.h>

static _Thread_local int h_errno_tls;

int *__h_errno_location(void)
{
	return &h_errno_tls;
}
