/* inet.c — inet_aton/pton -> inet_ntoa/ntop round-trip gate.
 *
 * Regression: inet_aton packed s_addr first-octet-most-significant, but
 * inet_ntoa/inet_ntop read raw host-memory bytes, reversing the quad on
 * little-endian (1.2.3.4 printed as 4.3.2.1, pton/ntop round-trip broken).
 */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>

static int
check4(const char *in, const char *want)
{
	struct in_addr a;
	char ntop[INET_ADDRSTRLEN];
	char ntoa[INET_ADDRSTRLEN];

	if (!inet_aton(in, &a)) {
		fprintf(stderr, "inet_aton(%s) failed\n", in);
		return 1;
	}
	/* inet_ntoa */
	const char *na = inet_ntoa(a);
	snprintf(ntoa, sizeof ntoa, "%s", na);
	if (strcmp(ntoa, want) != 0) {
		fprintf(stderr, "inet_ntoa(%s) = '%s', want '%s'\n", in, ntoa, want);
		return 1;
	}
	/* inet_ntop on the struct from inet_aton */
	if (!inet_ntop(AF_INET, &a, ntop, sizeof ntop) || strcmp(ntop, want) != 0) {
		fprintf(stderr, "inet_ntop(aton %s) = '%s', want '%s'\n", in, ntop, want);
		return 1;
	}
	/* inet_pton -> inet_ntop round-trip */
	if (!inet_pton(AF_INET, in, &a)) {
		fprintf(stderr, "inet_pton(%s) failed\n", in);
		return 1;
	}
	if (!inet_ntop(AF_INET, &a, ntop, sizeof ntop) || strcmp(ntop, in) != 0) {
		fprintf(stderr, "pton/ntop round-trip: '%s' != '%s'\n", ntop, in);
		return 1;
	}
	return 0;
}

int
main(void)
{
	if (check4("1.2.3.4", "1.2.3.4")) return 10;
	if (check4("127.0.0.1", "127.0.0.1")) return 11;
	if (check4("0.0.0.0", "0.0.0.0")) return 12;
	if (check4("255.255.255.255", "255.255.255.255")) return 13;
	if (check4("192.168.1.100", "192.168.1.100")) return 14;
	printf("PASS inet\n");
	return 0;
}
