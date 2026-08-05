#ifndef _REGEX_H
#define _REGEX_H

#include <stddef.h>
#include <sys/types.h>
#include <features.h>

typedef long long regoff_t;

typedef struct {
    size_t re_nsub;  /* number of parenthesized subexpressions */
    void  *opaque;   /* internal compiled pattern */
    size_t opaque_len;
} regex_t;

typedef struct {
    regoff_t rm_so;  /* byte offset from start of string */
    regoff_t rm_eo;  /* byte offset to end of match */
} regmatch_t;

/* REGEX cflags */
#define REG_EXTENDED  1
#define REG_ICASE     2
#define REG_NOSUB     4
#define REG_NEWLINE   8

/* REGEX eflags */
#define REG_NOTBOL    1
#define REG_NOTEOL    2

/* REGEX error codes */
#define REG_NOERROR   0
#define REG_NOMATCH   1
#define REG_BADPAT    2
#define REG_ECOLLATE  3
#define REG_ECTYPE    4
#define REG_EESCAPE   5
#define REG_ESUBREG   6
#define REG_EBRACK    7
#define REG_EPAREN    8
#define REG_EBRACE    9
#define REG_BADBR    10
#define REG_ERANGE   11
#define REG_ESPACE   12
#define REG_BADRPT   13

__BEGIN_DECLS
int regcomp(regex_t *restrict, const char *restrict, int);
int regexec(const regex_t *restrict, const char *restrict, size_t, regmatch_t [restrict], int);
size_t regerror(int, const regex_t *restrict, char *restrict, size_t);
void regfree(regex_t *);
__END_DECLS

#endif
