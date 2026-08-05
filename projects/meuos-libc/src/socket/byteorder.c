#include <stdint.h>
#include <netinet/in.h>

/* Host <-> network byte order.  Little-endian hosts swap; big-endian are
 * identity.  x86_64/aarch64/riscv64/loongarch64 are little-endian. */

uint32_t
htonl(uint32_t h)
{
	return ((h & 0x000000FFU) << 24) |
	       ((h & 0x0000FF00U) <<  8) |
	       ((h & 0x00FF0000U) >>  8) |
	       ((h & 0xFF000000U) >> 24);
}

uint32_t
ntohl(uint32_t n)
{
	return htonl(n);
}

uint16_t
htons(uint16_t h)
{
	return (uint16_t)(((h & 0x00FFU) << 8) | ((h & 0xFF00U) >> 8));
}

uint16_t
ntohs(uint16_t n)
{
	return htons(n);
}
