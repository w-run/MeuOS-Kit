#include <string.h>

static const char * const errstr[] = {
	[1]  = "Operation not permitted",
	[2]  = "No such file or directory",
	[4]  = "Interrupted system call",
	[5]  = "Input/output error",
	[7]  = "Argument list too long",
	[9]  = "Bad file descriptor",
	[10] = "No child processes",
	[11] = "Resource temporarily unavailable",
	[12] = "Cannot allocate memory",
	[13] = "Permission denied",
	[14] = "Bad address",
	[17] = "File exists",
	[20] = "Not a directory",
	[21] = "Is a directory",
	[22] = "Invalid argument",
	[23] = "Too many open files in system",
	[24] = "Too many open files",
	[28] = "No space left on device",
	[29] = "Illegal seek",
	[30] = "Read-only file system",
	[33] = "Numerical argument out of domain",
	[34] = "Numerical result out of range",
	[35] = "Resource deadlock avoided",
	[36] = "File name too long",
	[37] = "No locks available",
	[38] = "Function not implemented",
	[39] = "Directory not empty",
	[40] = "Too many levels of symbolic links",
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
