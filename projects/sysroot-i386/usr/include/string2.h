#ifndef MEUOS_STRING2_H
#define MEUOS_STRING2_H

#include <string.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* strdupa/strndupa: stack-allocated copies (glibc extension).
 * Require compiler-provided alloca (GCC __builtin_alloca). */
#ifndef __STRDUPA_DEFINED
#define __STRDUPA_DEFINED
#define strdupa(s) \
	(__extension__ ({ const char *_s = (s); \
	  (char *)memcpy(__builtin_alloca(strlen(_s)+1), _s, strlen(_s)+1); }))
#define strndupa(s, n) \
	(__extension__ ({ const char *_s = (s); size_t _n = (n); \
	  size_t _len = strnlen(_s, _n); \
	  char *_d = __builtin_alloca(_len+1); \
	  (char *)memcpy(_d, _s, _len); _d[_len] = '\0'; _d; }))
#endif

#ifdef __cplusplus
}
#endif

#endif
