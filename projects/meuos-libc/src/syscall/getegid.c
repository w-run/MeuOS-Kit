#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETEGID 108
uid_t getegid(void) { return (uid_t)__syscall0(LINUX_SYS_GETEGID); }
