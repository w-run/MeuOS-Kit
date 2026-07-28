/* socket/inet.c — IP address conversion functions */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

in_addr_t
inet_addr(const char *cp)
{
	struct in_addr a;
	if (inet_aton(cp, &a))
		return a.s_addr;
	return INADDR_NONE;
}

int
inet_aton(const char *cp, struct in_addr *inp)
{
	if (!cp || !*cp) return 0;
	unsigned int parts[4] = {0,0,0,0};
	int count = 0;
	int digit = 0;
	unsigned int val = 0;
	int base = 10;

	while (*cp && count < 4) {
		if (*cp == '.') {
			if (!digit) return 0; /* empty part */
			parts[count++] = val;
			val = 0; digit = 0; base = 10;
			cp++;
			continue;
		}
		if (count == 0 && !digit && *cp == '0') {
			base = 8; /* octal */
			cp++;
			continue;
		}
		if (base == 10 && !digit && *cp == 'x') {
			base = 16;
			cp++;
			continue;
		}
		int c = (unsigned char)*cp;
		int v = -1;
		if (c >= '0' && c <= '9') v = c - '0';
		else if (base >= 16 && c >= 'a' && c <= 'f') v = c - 'a' + 10;
		else if (base >= 16 && c >= 'A' && c <= 'F') v = c - 'A' + 10;
		else return 0;
		if (v >= base) return 0;
		val = val * base + v;
		if (val > 255 && count < 3) return 0; /* only last part can exceed 255 */
		digit = 1;
		cp++;
	}
	if (!digit) return 0;
	parts[count++] = val;

	switch (count) {
	case 1:
		inp->s_addr = parts[0];
		break;
	case 2:
		if (parts[0] > 255 || parts[1] > 0xffffff) return 0;
		inp->s_addr = (parts[0] << 24) | (parts[1]);
		break;
	case 3:
		if (parts[0] > 255 || parts[1] > 255 || parts[2] > 0xffff) return 0;
		inp->s_addr = (parts[0] << 24) | (parts[1] << 16) | parts[2];
		break;
	case 4:
		if (parts[0] > 255 || parts[1] > 255 || parts[2] > 255 || parts[3] > 255) return 0;
		inp->s_addr = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
		break;
	}
	return 1;
}

char *
inet_ntoa(struct in_addr in)
{
	static char buf[INET_ADDRSTRLEN];
	unsigned char *b = (unsigned char *)&in.s_addr;
	snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	return buf;
}

int
inet_pton(int af, const char *src, void *dst)
{
	if (af == AF_INET) {
		struct in_addr a;
		if (!inet_aton(src, &a)) return 0;
		*(struct in_addr *)dst = a;
		return 1;
	}
	/* AF_INET6 not implemented */
	return 0;
}

const char *
inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
	if (af == AF_INET) {
		const unsigned char *b = (const unsigned char *)src;
		if (size < INET_ADDRSTRLEN) return NULL;
		snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
		return dst;
	}
	return NULL;
}
