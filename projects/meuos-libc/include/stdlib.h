#ifndef MEUOS_STDLIB_H
#define MEUOS_STDLIB_H

#include <stddef.h>

void *malloc(size_t);
void free(void *);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
float strtof(const char *, char **);
int atoi(const char *);
double atof(const char *);
long atol(const char *);
long long atoll(const char *);
int abs(int);
long labs(long);
long long llabs(long long);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
extern char **environ;
char *getenv(const char *);
int setenv(const char *, const char *, int);
int unsetenv(const char *);
_Noreturn void exit(int);
_Noreturn void abort(void);
int atexit(void (*)(void));
int system(const char *);
int mkstemp(char *);
char *mktemp(char *);
char *mkdtemp(char *);
char *canonicalize_file_name(const char *);
char *realpath(const char *, char *);

#endif
