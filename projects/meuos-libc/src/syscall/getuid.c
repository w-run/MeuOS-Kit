#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETUID 102
uid_t getuid(void) { return (uid_t)__syscall0(LINUX_SYS_GETUID); }
