#include <errno.h>
#include <sys/mman.h>
#include "../internal/syscall.h"

#define LINUX_SYS_MMAP 9

void *
mmap(void *address, size_t length, int protection, int flags, int descriptor, off_t offset)
{
	long result;
#if defined(__i386__)
	/* i386 has no mmap(9); use mmap2(192) with offset in 4096-byte pages. */
	result = __syscall6(192, (long)address, length, protection, flags,
		descriptor, (long)(offset >> 12));
#else
	result = __syscall6(LINUX_SYS_MMAP, (long)address, length, protection,
		flags, descriptor, offset);
#endif
	if (__syscall_error(result)) {
		errno = (int)-result;
		return MAP_FAILED;
	}
	return (void *)result;
}
