#ifndef MEUOS_SYS_PARAM_H
#define MEUOS_SYS_PARAM_H

#include <limits.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif
#ifndef NOFILE
#define NOFILE 256
#endif
#ifndef NBBY
#define NBBY 8
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#ifndef howmany
#define howmany(x, y) (((x)+((y)-1))/(y))
#endif

#define DEV_BSIZE 512

#ifdef __cplusplus
}
#endif

#endif
