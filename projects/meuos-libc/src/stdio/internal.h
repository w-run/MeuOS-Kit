#ifndef MEUOS_STDIO_INTERNAL_H
#define MEUOS_STDIO_INTERNAL_H

/* Internal stdio definitions shared by the split stdio/.c files.
 * The __meuos_FILE layout and FILE_* flags must agree across every
 * translation unit that pokes at FILE internals; centralising them here
 * lets us split stdio.c by feature (file ops / char io / block io /
 * positioning / formatted I/O / cookie streams) without duplicating
 * the struct layout in every file. */

#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>

struct __meuos_FILE {
	int fd;
	unsigned flags;
	unsigned char *memory;
	size_t size;
	size_t pos;
	int ungot;
	pid_t pid;		/* popen() 子进程，pclose() 需要；非 popen 流为 0 */
	/* Cookie stream support (funopen/fopencookie). */
	void *cookie;
	ssize_t (*readfn)(void *, char *, size_t);
	ssize_t (*writefn)(void *, const char *, size_t);
	long (*seekfn)(void *, long, int);
	int (*closefn)(void *);
};

enum {
	FILE_READ   = 1u << 0,
	FILE_WRITE  = 1u << 1,
	FILE_MEMORY = 1u << 2,
	FILE_STATIC = 1u << 3,
	FILE_ERROR  = 1u << 4,
	FILE_COOKIE = 1u << 5,
};

extern struct __meuos_FILE __meuos_stdin;
extern struct __meuos_FILE __meuos_stdout;
extern struct __meuos_FILE __meuos_stderr;

/* Parse a mode string (e.g. "r+", "w", "a") into the open() flags
 * needed for the underlying file descriptor and the FILE_* flags
 * for the resulting stream. Returns -1 on invalid mode. */
int __meuos_stream_mode(const char *mode, int *open_flags, unsigned *stream_flags);

/* Formatted-output sink: used by both fprintf/printf and the in-memory
 * snprintf path. Each call to put() must return the character written
 * or EOF on error; the wrapper tracks the total character count. */
struct __meuos_print_sink {
	int (*put)(void *, int);
	void *context;
	int total;
};

int __meuos_sink_put(struct __meuos_print_sink *sink, int character);
int __meuos_sink_repeat(struct __meuos_print_sink *sink, int character, int count);
int __meuos_sink_number(struct __meuos_print_sink *sink,
    unsigned long long value, unsigned base, int width, int zero,
    int negative, const char *prefix);
int __meuos_vformat(struct __meuos_print_sink *sink, const char *format, va_list arguments);

/* Floating-point formatter for %f/%e/%g (and uppercase variants).
 * flags: bit 0='-', 1='+', 2=' ', 3='#', 4='0'.
 * precision: -1 for unspecified (resolved to 6), otherwise >= 0.
 * conv: one of 'f','F','e','E','g','G'; 'a','A' degrade to 'g','G'.
 * Returns 0 on success, -1 on sink error. */
int __meuos_fmt_fp(struct __meuos_print_sink *sink, double value, int conv,
    int width, int precision, int flags);

#endif
