/* dlfcn.c — dlopen/dlsym/dlclose/dlerror implementation.
 *
 * MeuOS dynamic loading phase 1: header + stub framework.
 * Full .so loading will be added in phase 2 once ld.so's internal
 * loader functions are exported for libc consumption. */

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- error state (single __thread buffer for now) ---- */
#define ERR_BUF_SIZE 256
static __thread char dl_err[ERR_BUF_SIZE];

static void
set_error(const char *msg)
{
	size_t n = strlen(msg);
	if (n >= ERR_BUF_SIZE) n = ERR_BUF_SIZE - 1;
	memcpy(dl_err, msg, n);
	dl_err[n] = '\0';
}

/* ---- dl* entry points ---- */

void *
dlopen(const char *file, int mode)
{
	(void)mode;
	if (!file || !*file) {
		set_error("filename is NULL");
		return NULL;
	}
	set_error("dlopen: not available yet (phase 2)");
	return NULL;
}

void *
dlsym(void *handle, const char *name)
{
	(void)handle;
	if (!name || !*name) {
		set_error("dlsym: symbol name is NULL");
		return NULL;
	}
	set_error("dlsym: not available yet (phase 2)");
	return NULL;
}

int
dlclose(void *handle)
{
	(void)handle;
	if (!handle)
		return -1;
	/* Stub: no-op, always success for null-like handles. */
	return 0;
}

char *
dlerror(void)
{
	char *e = dl_err;
	dl_err[0] = '\0';
	return *e ? e : NULL;
}
