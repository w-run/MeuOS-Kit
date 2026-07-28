#include <string.h>

static const char * const errstr[] = {
	[1]  = "Operation not permitted",
	[2]  = "No such file or directory",
	[4]  = "Interrupted system call",
	[9]  = "Bad file descriptor",
	[11] = "Resource temporarily unavailable",
	[12] = "Cannot allocate memory",
	[13] = "Permission denied",
	[14] = "Bad address",
	[22] = "Invalid argument",
	[38] = "Function not implemented",
};

char *
strerror(int error)
{
	if (error > 0 && (size_t)error < sizeof(errstr)/sizeof(errstr[0]) && errstr[error])
		return (char *)errstr[error];
	return "Unknown error";
}

int
strerror_r(int error, char *buf, size_t buflen)
{
	const char *msg = strerror(error);
	size_t len = strlen(msg) + 1;
	if (len > buflen) {
		memcpy(buf, msg, buflen - 1);
		buf[buflen - 1] = '\0';
		return ERANGE;
	}
	memcpy(buf, msg, len);
	return 0;
}
