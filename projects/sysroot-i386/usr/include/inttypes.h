#ifndef MEUOS_INTTYPES_H
#define MEUOS_INTTYPES_H

#include <stdint.h>

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

/* MeuOS x86_64 uses LP64. Other target sysroots must provide their own ABI
 * spelling before they are promoted to runnable libc targets. */
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"

#endif
