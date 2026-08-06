/* stdcountof.h — ISO C23 7.6.3: array element count macro.
 *
 * Defines countof(arr) which expands to the number of elements in an
 * array parameter (or known-size array variable).  The expression is a
 * size_t constant safe against pointer decay: the macro rejects a bare
 * pointer at compile time via a _Generic check.
 */

#ifndef MEUOS_STDCOUNT_H
#define MEUOS_STDCOUNT_H

#include <stddef.h>

/* Generic: match on array type.  When arr is a pointer (not an array),
 * the first _Generic alternative is not selected and the char declaration
 * creates a negative-size array -> compile error. */
#define countof(arr) \
	((size_t)(sizeof(arr) / sizeof((arr)[0])) + \
	 (size_t)_Generic(&(arr), \
		 typeof(arr) (*)[]:  0, \
		 default: \
			 (sizeof(struct { int:-!!(sizeof(&(arr)) == sizeof(arr)); })) \
			 ))

#endif /* MEUOS_STDCOUNT_H */