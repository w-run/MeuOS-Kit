#include <string.h>

char *
strerror(int error)
{
	switch (error) {
	case 1: return "Operation not permitted";
	case 2: return "No such file or directory";
	case 4: return "Interrupted system call";
	case 9: return "Bad file descriptor";
	case 11: return "Resource temporarily unavailable";
	case 12: return "Cannot allocate memory";
	case 13: return "Permission denied";
	case 14: return "Bad address";
	case 22: return "Invalid argument";
	case 38: return "Function not implemented";
	default: return "Unknown error";
	}
}
