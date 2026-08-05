/* bit.c — bit-manipulation helpers (libgcc ABI).
 *
 * __clzsi2/__clzdi2   — count leading zeros
 * __ctzsi2/__ctzdi2   — count trailing zeros
 * __popcountsi2/__popcountdi2
 * __paritysi2/__paritydi2
 * __bswapsi2/__bswapdi2
 *
 * Pure C, no target builtins, so they link on any arch without libgcc.
 */

#include "libgcc_int.h"

uword
__clzsi2(uword x)
{
	uword n = 0;
	if (!x)
		return 32;
	if (!(x & 0xFFFF0000u)) { n += 16; x <<= 16; }
	if (!(x & 0xFF000000u)) { n += 8;  x <<= 8;  }
	if (!(x & 0xF0000000u)) { n += 4;  x <<= 4;  }
	if (!(x & 0xC0000000u)) { n += 2;  x <<= 2;  }
	if (!(x & 0x80000000u)) { n += 1; }
	return n;
}

uword
__clzdi2(du_int x)
{
	uword hi = (uword)(x >> 32);
	if (hi)
		return __clzsi2(hi);
	return 32 + __clzsi2((uword)x);
}

uword
__ctzsi2(uword x)
{
	uword n = 0;
	if (!x)
		return 32;
	if (!(x & 0x0000FFFFu)) { n += 16; x >>= 16; }
	if (!(x & 0x000000FFu)) { n += 8;  x >>= 8;  }
	if (!(x & 0x0000000Fu)) { n += 4;  x >>= 4;  }
	if (!(x & 0x00000003u)) { n += 2;  x >>= 2;  }
	if (!(x & 0x00000001u)) { n += 1; }
	return n;
}

uword
__ctzdi2(du_int x)
{
	uword lo = (uword)x;
	if (lo)
		return __ctzsi2(lo);
	return 32 + __ctzsi2((uword)(x >> 32));
}

uword
__popcountsi2(uword x)
{
	x = x - ((x >> 1) & 0x55555555u);
	x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
	x = (x + (x >> 4)) & 0x0F0F0F0Fu;
	x = x + (x >> 8);
	x = x + (x >> 16);
	return x & 0x3Fu;
}

uword
__popcountdi2(du_int x)
{
	return __popcountsi2((uword)x) + __popcountsi2((uword)(x >> 32));
}

uword
__paritysi2(uword x)
{
	return __popcountsi2(x) & 1u;
}

uword
__paritydi2(du_int x)
{
	return __popcountdi2(x) & 1u;
}

uword
__bswapsi2(uword x)
{
	return ((x & 0x000000FFu) << 24) |
	       ((x & 0x0000FF00u) <<  8) |
	       ((x & 0x00FF0000u) >>  8) |
	       ((x & 0xFF000000u) >> 24);
}

du_int
__bswapdi2(du_int x)
{
	du_int lo = __bswapsi2((uword)x);
	du_int hi = __bswapsi2((uword)(x >> 32));
	return (lo << 32) | hi;
}
