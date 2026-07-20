#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

/* Definitions for the globals declared in error.h. */
unsigned int error_message_count;
int error_one_per_line;
void (*error_print_progname)(void);

static const char *progname;

/* Programs link against __progname (glibc provides it); provide a weak
 * fallback so the symbol resolves even if the program never sets it. */
extern const char *__progname __attribute__((weak));

static void
print_progname(void)
{
	if (error_print_progname) {
		error_print_progname();
		return;
	}
	if (!progname) {
		if (&__progname && __progname)
			progname = __progname;
		else
			progname = "";
	}
	if (progname && *progname)
		fprintf(stderr, "%s: ", progname);
}

static void
vreport(int errnum, const char *fname, unsigned int lineno,
    const char *format, va_list ap)
{
	static const char *last_fname;
	static unsigned int last_lineno;

	if (error_one_per_line && fname && fname == last_fname && lineno == last_lineno)
		return;
	if (fname) {
		fprintf(stderr, "%s:%u: ", fname, lineno);
		last_fname = fname;
		last_lineno = lineno;
	} else {
		print_progname();
	}
	if (format) {
		vfprintf(stderr, format, ap);
	}
	if (errnum)
		fprintf(stderr, ": %s", strerror(errnum));
	fprintf(stderr, "\n");
	fflush(stderr);
	++error_message_count;
}

void
error(int status, int errnum, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vreport(errnum, (const char *)0, 0, format, ap);
	va_end(ap);
	if (status)
		exit(status);
}

void
error_at_line(int status, int errnum, const char *fname,
    unsigned int lineno, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vreport(errnum, fname, lineno, format, ap);
	va_end(ap);
	if (status)
		exit(status);
}
