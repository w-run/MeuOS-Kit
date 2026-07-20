#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_GETGID 104
uid_t getgid(void) { return (uid_t)__syscall0(LINUX_SYS_GETGID); }
