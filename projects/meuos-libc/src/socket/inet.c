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
	/* Part prefix: 0x/0X -> hex (consume '0' and 'x'); leading '0' alone
	 * -> octal; else decimal.  The leading '0' of octal is not consumed so
	 * a bare "0" part still parses to value 0 ("0.0.0.0" works). */
	if (!digit && *cp == '0') {
		if (cp[1] == 'x' || cp[1] == 'X') {
			base = 16;
			cp += 2;
			continue;
		}
		base = 8;
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
	/* s_addr is the dotted quad packed first-octet-most-significant (as
	 * inet_aton stores it); extract high byte first so it is correct on
	 * both endiannesses (raw memory-byte reads would be reversed on LE). */
	uint32_t x = in.s_addr;
	snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
		(x >> 24) & 0xff, (x >> 16) & 0xff, (x >> 8) & 0xff, x & 0xff);
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
		if (size < INET_ADDRSTRLEN) return NULL;
		/* s_addr packed first-octet-most-significant by inet_aton/pton;
		 * extract high byte first (endian-independent). */
		uint32_t x = ((const struct in_addr *)src)->s_addr;
		snprintf(dst, size, "%u.%u.%u.%u",
			(x >> 24) & 0xff, (x >> 16) & 0xff, (x >> 8) & 0xff, x & 0xff);
		return dst;
	}
	return NULL;
}
