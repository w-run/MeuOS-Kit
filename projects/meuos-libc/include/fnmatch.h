#ifndef _FNMATCH_H
#define _FNMATCH_H

#define FNM_NOMATCH 1
#define FNM_PATHNAME 1
#define FNM_PERIOD 4
#define FNM_NOESCAPE 8
#define FNM_CASEFOLD 16

int fnmatch(const char *, const char *, int);

#endif
