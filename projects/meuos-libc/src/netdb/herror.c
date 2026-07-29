#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

void
herror(const char *s)
{
	if (s && *s)
		fprintf(stderr, "%s: %s\n", s, hstrerror(h_errno));
	else
		fprintf(stderr, "%s\n", hstrerror(h_errno));
}

const char *
hstrerror(int err)
{
	switch (err) {
	case HOST_NOT_FOUND: return "Unknown host";
	case TRY_AGAIN:      return "Host name lookup failure";
	case NO_RECOVERY:    return "Non-recoverable name lookup error";
	case NO_DATA:
		return "No address associated with name";
	default:             return "Unknown resolver error";
	}
}
