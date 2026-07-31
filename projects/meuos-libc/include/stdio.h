#ifndef MEUOS_STDIO_H
#define MEUOS_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define EOF (-1)
#define L_tmpnam 20
#define BUFSIZ 8192

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef long fpos_t;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* The current runtime implements the console subset below.  Keep FILE opaque
 * so hosted tools can compile against the stable interface while file streams
 * are completed as the next libc milestone. */
typedef struct __meuos_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int putchar(int);
int getchar(void);
int getc(FILE *);
int fgetc(FILE *);
int ungetc(int, FILE *);
int putc(int, FILE *);
int puts(const char *);
int fputc(int, FILE *);
int fputs(const char *, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int vsprintf(char *, const char *, va_list);
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
FILE *fdopen(int, const char *);
FILE *popen(const char *, const char *);
int pclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);
int feof(FILE *);
int fgetpos(FILE *, fpos_t *);
int fsetpos(FILE *, const fpos_t *);
int remove(const char *);
char *tmpnam(char *);
FILE *tmpfile(void);
int fileno(FILE *);
int ftruncate(int, off_t);

FILE *fmemopen(void *, size_t, const char *);
typedef struct {
	ssize_t (*read)(void *, char *, size_t);
	ssize_t (*write)(void *, const char *, size_t);
	int (*seek)(void *, long *, int);
	int (*close)(void *);
} cookie_io_functions_t;
FILE *fopencookie(void *, const char *, cookie_io_functions_t);
FILE *funopen(const void *, int (*)(void *, char *, int),
    int (*)(void *, const char *, int),
    long (*)(void *, long, int), int (*)(void *));
int fclose(FILE *);
int fflush(FILE *);
int ferror(FILE *);
int setvbuf(FILE *, char *, int, size_t);
void setbuf(FILE *, char *);
void setlinebuf(FILE *);
char *fgets(char *, int, FILE *);
ssize_t getline(char **, size_t *, FILE *);
ssize_t getdelim(char **, size_t *, int, FILE *);
int asprintf(char **, const char *, ...);
int vasprintf(char **, const char *, va_list);
int vprintf(const char *, va_list);
int printf(const char *, ...);
int vsscanf(const char *, const char *, va_list);
int sscanf(const char *, const char *, ...);
int scanf(const char *, ...);
void perror(const char *);

#endif
