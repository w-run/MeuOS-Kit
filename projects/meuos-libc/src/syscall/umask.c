#include <sys/types.h>
#include <sys/stat.h>
#include "../internal/syscall.h"
#define LINUX_SYS_UMASK 95
mode_t umask(mode_t mask) { return (mode_t)__syscall1(LINUX_SYS_UMASK, (long)mask); }
