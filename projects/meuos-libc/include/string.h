#ifndef MEUOS_STRING_H
#define MEUOS_STRING_H

#include <features.h>
#include <stddef.h>
#include <errno.h>

__BEGIN_DECLS
void *memcpy(void *restrict, const void *restrict, size_t);
void *memmove(void *, const void *, size_t);
void *memccpy(void *restrict, const void *restrict, int, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);

size_t strlen(const char *);
size_t strnlen(const char *, size_t);
char *strcpy(char *restrict, const char *restrict);
char *strncpy(char *restrict, const char *restrict, size_t);
char *strcat(char *restrict, const char *restrict);
char *strncat(char *restrict, const char *restrict, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcoll(const char *, const char *);
size_t strxfrm(char *restrict, const char *restrict, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strpbrk(const char *, const char *);
char *strstr(const char *, const char *);
char *strdup(const char *);
char *strndup(const char *, size_t);
char *strerror(int);
int strerror_r(int, char *, size_t);
char *strsignal(int);
char *strtok(char *restrict, const char *restrict);
char *strtok_r(char *restrict, const char *restrict, char **restrict);
size_t strspn(const char *, const char *);
size_t strcspn(const char *, const char *);
int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);
__END_DECLS

#endif
