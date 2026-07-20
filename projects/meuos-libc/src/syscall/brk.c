#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* Linux x86_64 syscall number for brk. */
#define LINUX_SYS_BRK 12

int
brk(void *address)
{
	long result = __syscall1(LINUX_SYS_BRK, (long)address);

	/* Linux returns the current break, rather than an errno value, on failure. */
	if ((void *)result != address) {
		errno = ENOMEM;
		return -1;
	}
	return 0;
}

void *
sbrk(intptr_t increment)
{
	long previous = __syscall1(LINUX_SYS_BRK, 0);
	long requested;
	long result;

	if (increment == 0)
		return (void *)previous;
	if ((increment > 0 && previous > (long)((unsigned long)-1 >> 1) - increment) ||
	    (increment < 0 && previous < (-((long)((unsigned long)-1 >> 1)) - 1) - increment)) {
		errno = ENOMEM;
		return (void *)-1;
	}
	requested = previous + increment;
	result = __syscall1(LINUX_SYS_BRK, requested);
	if (result != requested) {
		errno = ENOMEM;
		return (void *)-1;
	}
	return (void *)previous;
}
