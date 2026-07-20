#include "../internal/syscall.h"

/* Raw result is required before the crt has installed errno/TLS. */
#define LINUX_SYS_ARCH_PRCTL 158

long
__meuos_arch_prctl(long code, unsigned long address)
{
	return __syscall2(LINUX_SYS_ARCH_PRCTL, code, (long)address);
}
