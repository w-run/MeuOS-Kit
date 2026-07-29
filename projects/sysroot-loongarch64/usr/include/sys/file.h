#ifndef MEUOS_SYS_FILE_H
#define MEUOS_SYS_FILE_H

#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8

int flock(int, int);

#ifdef __cplusplus
}
#endif

#endif
