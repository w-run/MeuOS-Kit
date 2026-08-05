#ifndef _WORDEXP_H
#define _WORDEXP_H

#include <stddef.h>
#include <features.h>

/* wordexp_t: array of expanded words.  we_offs entries are NULL at the
 * start when WRDE_DOOFFS is set (reserved for caller use). */
typedef struct {
	size_t we_wordc;    /* count of words */
	char **we_wordv;    /* NULL-terminated array of words */
	size_t we_offs;     /* number of leading NULL slots */
} wordexp_t;

/* Flags for wordexp. */
#define WRDE_APPEND 1    /* append to the results from a previous call */
#define WRDE_DOOFFS 2    /* keep we_offs leading NULL entries */
#define WRDE_NOCMD  4    /* do not do command substitution */
#define WRDE_REUSE  8    /* reuse storage from a previous call (glibc) */
#define WRDE_SHOWERR 16  /* do not redirect stderr from command substitution */
#define WRDE_UNDEF  32   /* undefined variables are an error */

/* Error codes for wordexp. */
#define WRDE_SUCCESS 0
#define WRDE_NOSPACE 1   /* cannot allocate memory */
#define WRDE_BADCHAR 2   /* illegal occurrence of a special char */
#define WRDE_BADVAL  3   /* undefined variable (with WRDE_UNDEF) */
#define WRDE_CMDSUB  4   /* command substitution requested but the shell
                            cannot/was not asked to (WRDE_NOCMD) */
#define WRDE_SYNTAX  5   /* syntax error in the input word */

/* glibc extensions widely relied on. */
#define WRDE_NOSYS   6

__BEGIN_DECLS
int wordexp(const char *restrict words, wordexp_t *restrict pwordexp,
            int flags);
void wordfree(wordexp_t *pwordexp);
__END_DECLS

#endif
