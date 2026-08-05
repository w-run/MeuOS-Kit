#include <errno.h>
#include <unistd.h>
#include <features.h>
#include "../internal/syscall.h"
#define LINUX_SYS_SYSCONF 158

/* 注意：case 值必须与 include/unistd.h 中的 _PC_* / _SC_* 常量严格一致。
 * 此前 case 编号与常量错位两号，导致 sysconf(_SC_ARG_MAX) 落入 default
 * 返回 EINVAL、sysconf(_SC_PAGESIZE) 返回 1 等错误结果。 */
long pathconf(const char *path, int name) {
	(void)path;
	switch (name) {
	case _PC_LINK_MAX: return 256;
	case _PC_NAME_MAX: return 255;
	case _PC_PATH_MAX: return 4096;
	case _PC_PIPE_BUF: return 4096;
	case _PC_CHOWN_RESTRICTED: return 8;
	default: errno = EINVAL; return -1;
	}
}

long fpathconf(int fd, int name) { return pathconf("", name); }

long sysconf(int name) {
	switch (name) {
	case _SC_ARG_MAX: return 256;
	case _SC_CHILD_MAX: return 256;
	case _SC_CLK_TCK: return 64;
	case _SC_NGROUPS_MAX: return 8192;
	case _SC_OPEN_MAX: return 256;
	case _SC_JOB_CONTROL: return 1;
	case _SC_SAVED_IDS: return 1;
	case _SC_VERSION: return _POSIX_VERSION;
	case _SC_TZNAME: return 1;
	case _SC_PAGESIZE: return 4096;
	case _SC_PHYS_PAGES: return 2;
	case _SC_GETPW_R_SIZE_MAX: return 4096;
	case _SC_ATEXIT_MAX: return 1;
	case _SC_NPROCESSORS_ONLN: return 1;
	default: errno = EINVAL; return -1;
	}
}
