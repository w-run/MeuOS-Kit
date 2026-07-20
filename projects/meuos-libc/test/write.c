#include <errno.h>
#include <unistd.h>

int
main(void)
{
	if (getpid() <= 0 || close(-1) != -1 || errno != EBADF)
		return 1;
	if (write(-1, "", 0) != -1 || errno != EBADF)
		return 1;
	return write(1, "PASS\n", 5) == 5 ? 0 : 1;
}
