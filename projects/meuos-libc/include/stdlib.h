#ifndef MEUOS_STDLIB_H
#define MEUOS_STDLIB_H

#include <features.h>
#include <stddef.h>

#define RAND_MAX 32767

typedef struct {
	int quot;
	int rem;
} div_t;

typedef struct {
	long quot;
	long rem;
} ldiv_t;

__BEGIN_DECLS
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
int rand(void);
void srand(unsigned int);
div_t div(int, int);
ldiv_t ldiv(long, long);
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
__END_DECLS

/* C99 7.20.7 multibyte/wide character conversion (single-byte locale) */
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);

#endif
