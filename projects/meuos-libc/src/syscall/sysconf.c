#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_SYSCONF 158

long pathconf(const char *path, int name) {
	(void)path;
	switch (name) {
	case 1: return 256;   /* _PC_LINK_MAX */
	case 4: return 255;   /* _PC_NAME_MAX */
	case 5: return 4096;  /* _PC_PATH_MAX */
	case 6: return 4096;  /* _PC_PIPE_BUF */
	case 7: return 8;     /* _PC_CHOWN_RESTRICTED */
	default: errno = EINVAL; return -1;
	}
}

long fpathconf(int fd, int name) { return pathconf("", name); }

long sysconf(int name) {
	switch (name) {
	case 2: return 256;   /* _SC_ARG_MAX */
	case 3: return 256;   /* _SC_CHILD_MAX */
	case 4: return 64;    /* _SC_CLK_TCK */
	case 5: return 8192;  /* _SC_NGROUPS_MAX */
	case 6: return 256;   /* _SC_OPEN_MAX */
	case 7: return 1;     /* _SC_JOB_CONTROL */
	case 8: return 1;     /* _SC_SAVED_IDS */
	case 9: return 1;     /* _SC_VERSION */
	case 10: return 1;    /* _SC_STREAMS */
	case 11: return 1;    /* _SC_TZNAME */
	case 12: return 1;    /* _SC_PAGESIZE / _SC_PAGE_SIZE */
	case 13: return 4096; /* _SC_PAGESIZE */
	case 14: return 2;    /* _SC_PHYS_PAGES fallback */
	case 24: return 4096; /* _SC_GETPW_R_SIZE_MAX */
	case 39: return 1;    /* _SC_ATEXIT_MAX */
	default: errno = EINVAL; return -1;
	}
}
