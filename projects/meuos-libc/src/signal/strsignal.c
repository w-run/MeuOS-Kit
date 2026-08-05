#include <signal.h>
#include <stdio.h>
#include <string.h>

/* strsignal(3): descriptive string for a signal number.  Uses a small
 * static buffer per call (glibc returns a shared static buffer). */

static char shared[32];

static const char *
sig_name(int sig)
{
	switch (sig) {
	case SIGHUP:    return "Hangup";
	case SIGINT:    return "Interrupt";
	case SIGQUIT:   return "Quit";
	case SIGILL:    return "Illegal instruction";
	case SIGTRAP:   return "Trace/breakpoint trap";
	case SIGABRT:   return "Aborted";
	case SIGBUS:    return "Bus error";
	case SIGFPE:    return "Floating point exception";
	case SIGKILL:   return "Killed";
	case SIGUSR1:   return "User defined signal 1";
	case SIGSEGV:   return "Segmentation fault";
	case SIGUSR2:   return "User defined signal 2";
	case SIGPIPE:   return "Broken pipe";
	case SIGALRM:   return "Alarm clock";
	case SIGTERM:   return "Terminated";
	default:        return NULL;
	}
}

char *
strsignal(int sig)
{
	const char *name = sig_name(sig);
	const char *body;

	if (name) {
		(void)snprintf(shared, sizeof shared, "%s (%d)", name, sig);
		return shared;
	}
	body = "Unknown signal";
	(void)snprintf(shared, sizeof shared, "%s (%d)", body, sig);
	return shared;
}
