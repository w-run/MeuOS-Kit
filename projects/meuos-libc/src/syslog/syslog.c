/* syslog/syslog.c — POSIX system logger */

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct {
	const char *ident;
	int options;
	int facility;
	int opened;
} log_state = { NULL, 0, LOG_USER, 0 };

static const char * const priority_str[] = {
	"emerg", "alert", "crit", "err",
	"warning", "notice", "info", "debug"
};

void
openlog(const char *ident, int options, int facility)
{
	log_state.ident = ident;
	log_state.options = options;
	log_state.facility = facility ? facility : LOG_USER;
	log_state.opened = 1;
}

void
closelog(void)
{
	log_state.opened = 0;
	log_state.ident = NULL;
}

void
vsyslog(int priority, const char *format, va_list ap)
{
	int pri = LOG_PRI(priority);
	int fac = priority & ~LOG_PRIMASK;
	if (!fac) fac = log_state.facility;
	(void)fac;

	/* Format the message */
	char msg[4096];
	vsnprintf(msg, sizeof(msg), format, ap);

	/* Write to stderr if LOG_PERROR or no syslog available */
	FILE *out = stderr;
	if (log_state.options & LOG_PERROR || !log_state.opened)
		out = stderr;

	/* Timestamp */
	char timestamp[64];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	if (tm)
		strftime(timestamp, sizeof(timestamp), "%b %e %T", tm);
	else
		timestamp[0] = '\0';

	const char *ident = log_state.ident ? log_state.ident : "<unnamed>";
	const char *pri_name = (pri >= 0 && pri <= 7) ? priority_str[pri] : "?";

	fprintf(out, "%s %s %s[%d]: <%s> %s\n",
	        timestamp, ident, "syslog", (int)getpid(), pri_name, msg);

	/* Also try /dev/console if LOG_CONS */
	if (log_state.options & LOG_CONS) {
		FILE *cons = fopen("/dev/console", "w");
		if (cons) {
			fprintf(cons, "%s: %s\n", ident, msg);
			fclose(cons);
		}
	}
}

void
syslog(int priority, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsyslog(priority, format, ap);
	va_end(ap);
}
