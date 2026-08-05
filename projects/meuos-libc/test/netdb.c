/* netdb.c — regression tests for <netdb.h> implementation.
 * Tests: getaddrinfo, getnameinfo, gai_strerror, getprotobyname/number, hstrerror */

#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Inline byte-swap for network order (big-endian).
 * htonl/ntohl are not yet in meuos-libc headers. */
static unsigned
to_net32(unsigned v)
{
	return ((v & 0xff) << 24) | ((v & 0xff00) << 8)
	     | ((v & 0xff0000) >> 8) | ((v >> 24) & 0xff);
}
static unsigned short
to_net16(unsigned short v)
{
	return (unsigned short)((v << 8) | (v >> 8));
}

int main(void) {
	int failures = 0;

	/* Test 1: getaddrinfo with AI_NUMERICHOST (IPv4) */
	{
		struct addrinfo hints, *res;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;

		int ret = getaddrinfo("127.0.0.1", "80", &hints, &res);
		if (ret != 0) {
			printf("FAIL: getaddrinfo 127.0.0.1:80 => %s (%d)\n", gai_strerror(ret), ret);
			failures++;
		} else {
			if (res->ai_family != AF_INET) {
				printf("FAIL: getaddrinfo family expected AF_INET, got %d\n", res->ai_family);
				failures++;
			}
			struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
			/* sin_addr.s_addr is in network byte order; 127.0.0.1 = 0x7f000001 */
			if (sin->sin_addr.s_addr != 0x7f000001) {
				printf("FAIL: getaddrinfo addr expected 127.0.0.1 (0x7f000001), got %08x\n",
				       (unsigned)sin->sin_addr.s_addr);
				failures++;
			}
			if (sin->sin_port != to_net16(80)) {
				printf("FAIL: getaddrinfo port expected 80, got %d\n", (int)sin->sin_port);
				failures++;
			}
			if (failures == 0)
				printf("PASS: getaddrinfo 127.0.0.1:80\n");
			freeaddrinfo(res);
		}
	}

	/* Test 2: getaddrinfo with NULL node (AI_PASSIVE) */
	{
		struct addrinfo hints, *res;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

		int ret = getaddrinfo(NULL, "8080", &hints, &res);
		if (ret != 0) {
			printf("FAIL: getaddrinfo NULL:8080 => %s (%d)\n", gai_strerror(ret), ret);
			failures++;
		} else {
			printf("PASS: getaddrinfo NULL:8080\n");
			freeaddrinfo(res);
		}
	}

	/* Test 3: getnameinfo with NI_NUMERICHOST */
	{
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = 0x01020304; /* 1.2.3.4 (network order = 1st octet MSB; no byte-swap, cf. 127.0.0.1=0x7f000001 above) */
		sa.sin_port = to_net16(443);

		char host[256], serv[32];
		int ret = getnameinfo((struct sockaddr *)&sa, sizeof(sa),
		                     host, sizeof(host), serv, sizeof(serv),
		                     NI_NUMERICHOST | NI_NUMERICSERV);
		if (ret != 0) {
			printf("FAIL: getnameinfo => %s (%d)\n", gai_strerror(ret), ret);
			failures++;
		} else {
			if (strcmp(host, "1.2.3.4") != 0) {
				printf("FAIL: getnameinfo host expected '1.2.3.4', got '%s'\n", host);
				failures++;
			}
			if (strcmp(serv, "443") != 0) {
				printf("FAIL: getnameinfo serv expected '443', got '%s'\n", serv);
				failures++;
			}
			if (failures == 0)
				printf("PASS: getnameinfo 1.2.3.4:443\n");
		}
	}

	/* Test 4: gai_strerror */
	{
		const char *s = gai_strerror(EAI_NONAME);
		if (!s || !*s) {
			printf("FAIL: gai_strerror(EAI_NONAME) empty\n");
			failures++;
		} else {
			printf("PASS: gai_strerror(EAI_NONAME) = \"%s\"\n", s);
		}
	}

	/* Test 5: getprotobyname / getprotobynumber */
	{
		struct protoent *pe = getprotobyname("tcp");
		if (!pe || pe->p_proto != 6) {
			printf("FAIL: getprotobyname('tcp') expected proto=6\n");
			failures++;
		} else {
			printf("PASS: getprotobyname('tcp') = %d\n", pe->p_proto);
		}

		pe = getprotobynumber(17);
		if (!pe || strcmp(pe->p_name, "udp") != 0) {
			printf("FAIL: getprotobynumber(17) expected 'udp'\n");
			failures++;
		} else {
			printf("PASS: getprotobynumber(17) = '%s'\n", pe->p_name);
		}
	}

	/* Test 6: hstrerror */
	{
		const char *s = hstrerror(HOST_NOT_FOUND);
		if (!s || !*s) {
			printf("FAIL: hstrerror(HOST_NOT_FOUND) empty\n");
			failures++;
		} else {
			printf("PASS: hstrerror(HOST_NOT_FOUND) = \"%s\"\n", s);
		}
	}

	if (failures > 0) {
		printf("\n%d test(s) FAILED\n", failures);
		return 1;
	}
	printf("\nAll netdb tests PASS\n");
	return 0;
}
