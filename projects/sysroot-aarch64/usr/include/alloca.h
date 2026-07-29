#ifndef MEUOS_ALLOCA_H
#define MEUOS_ALLOCA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* alloca is a compiler builtin on most modern compilers. */
void *alloca(size_t);

#ifdef __cplusplus
}
#endif

#endif
