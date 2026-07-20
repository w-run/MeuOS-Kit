#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETEUID 107
uid_t geteuid(void) { return (uid_t)__syscall0(LINUX_SYS_GETEUID); }
