#ifndef _DLFCN_H
#define _DLFCN_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open modes for dlopen */
#define RTLD_LAZY    1
#define RTLD_NOW     2
#define RTLD_GLOBAL  0x100
#define RTLD_LOCAL   0

/* Special handle values for dlsym */
#define RTLD_DEFAULT  ((void *)0)
#define RTLD_NEXT     ((void *)-1)

/* dl* interface */
void  *dlopen(const char *__restrict, int);
void  *dlsym(void *__restrict, const char *__restrict);
int    dlclose(void *);
char  *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif /* _DLFCN_H */
