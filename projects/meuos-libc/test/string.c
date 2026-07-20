#include <string.h>

extern int puts(const char *);

int
main(void)
{
	char buffer[16];
	char overlap[8] = "abcdef";
	char tokens[] = "one,two,,three";
	char *state;

	memset(buffer, 0, sizeof(buffer));
	strcpy(buffer, "hello");
	if (strlen(buffer) != 5 || strcmp(buffer, "hello") != 0 ||
	    strchr(buffer, 'l') != buffer + 2 || strrchr(buffer, 'l') != buffer + 3) {
		puts("FAIL");
		return 1;
	}
	strncpy(buffer, "xy", sizeof(buffer));
	if (buffer[0] != 'x' || buffer[1] != 'y' || buffer[2] != 0 ||
	    memcmp(buffer, "xy", 3) != 0) {
		puts("FAIL");
		return 1;
	}
	memmove(overlap + 1, overlap, 6);
	if (strcmp(overlap, "aabcdef") != 0 || memchr(overlap, 'd', 7) != overlap + 4) {
		puts("FAIL");
		return 1;
	}
	if (strcmp(strerror(12), "Cannot allocate memory") != 0) {
		puts("FAIL");
		return 1;
	}
	if (strcmp(strtok_r(tokens, ",", &state), "one") || strcmp(strtok_r(0, ",", &state), "two") || strcmp(strtok_r(0, ",", &state), "three") || strtok_r(0, ",", &state)) {
		puts("FAIL");
		return 1;
	}
	puts("PASS");
	return 0;
}
