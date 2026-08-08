/* stdbit.h — ISO C23 7.18: bit and byte utilities.
 *
 * Declares the type-generic function-like macros for counting / finding
 * / testing bits.  MeuOS libc implements them through its own C routines
 * (avoiding __builtin_* that mcc does not recognise) while remaining
 * compile-time evaluable for constants through if-else chains.
 *
 * NOTE: unsigned long is 64-bit on all primary MeuOS targets (LP64).
 * i386/arm (32-bit unsigned long) need separate _Generic entries; they
 * can be added when those archs reach P0 parity.
 */

#ifndef MEUOS_STDBIT_H
#define MEUOS_STDBIT_H

#include <stdint.h>

/* ---- internal helpers (static inline, header-only) ---- */

static inline unsigned
__stdc_clz32(unsigned x)
{
	unsigned n = 0;
	if (!x) return 32;
	if (!(x & 0xFFFF0000u)) { n += 16; x <<= 16; }
	if (!(x & 0xFF000000u)) { n += 8;  x <<= 8;  }
	if (!(x & 0xF0000000u)) { n += 4;  x <<= 4;  }
	if (!(x & 0xC0000000u)) { n += 2;  x <<= 2;  }
	if (!(x & 0x80000000u)) { n += 1; }
	return n;
}

static inline unsigned
__stdc_ctz32(unsigned x)
{
	unsigned n = 0;
	if (!x) return 32;
	if (!(x & 0x0000FFFFu)) { n += 16; x >>= 16; }
	if (!(x & 0x000000FFu)) { n += 8;  x >>= 8;  }
	if (!(x & 0x0000000Fu)) { n += 4;  x >>= 4;  }
	if (!(x & 0x00000003u)) { n += 2;  x >>= 2;  }
	if (!(x & 0x00000001u)) { n += 1; }
	return n;
}

static inline unsigned
__stdc_clz64(unsigned long long x)
{
	unsigned hi = (unsigned)(x >> 32);
	if (hi) return __stdc_clz32(hi);
	return 32 + __stdc_clz32((unsigned)x);
}

static inline unsigned
__stdc_ctz64(unsigned long long x)
{
	unsigned lo = (unsigned)x;
	if (lo) return __stdc_ctz32(lo);
	return 32 + __stdc_ctz32((unsigned)(x >> 32));
}

/* 窄类型（8/16 位）：以自身位宽计算 clz/ctz，0 特判返回自身位宽。
 * clz：先左移到 32 位顶端再数前导零（等价于在窄位宽内数）；
 * ctz：低位不受提升影响，直接数即可。 */
static inline unsigned
__stdc_clz8(unsigned char x)
{
	return x ? __stdc_clz32((unsigned)x << 24) : 8u;
}
static inline unsigned
__stdc_clz16(unsigned short x)
{
	return x ? __stdc_clz32((unsigned)x << 16) : 16u;
}
static inline unsigned
__stdc_ctz8(unsigned char x)
{
	return x ? __stdc_ctz32((unsigned)x) : 8u;
}
static inline unsigned
__stdc_ctz16(unsigned short x)
{
	return x ? __stdc_ctz32((unsigned)x) : 16u;
}

static inline unsigned
__stdc_popcount32(unsigned x)
{
	x = x - ((x >> 1) & 0x55555555u);
	x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
	x = (x + (x >> 4)) & 0x0F0F0F0Fu;
	x = x + (x >> 8);
	x = x + (x >> 16);
	return x & 0x3Fu;
}

static inline unsigned
__stdc_popcount64(unsigned long long x)
{
	return __stdc_popcount32((unsigned)x)
	     + __stdc_popcount32((unsigned)(x >> 32));
}

/* ---- 7.18.2.1: count of leading/trailing zero/one bits ---- */

#define stdc_leading_zeros(x) \
	_Generic((x), \
		unsigned char:		(unsigned)__stdc_clz8((unsigned char)(x)), \
		unsigned short:		(unsigned)__stdc_clz16((unsigned short)(x)), \
		unsigned int:		(unsigned)__stdc_clz32(x), \
		unsigned long:		(unsigned)__stdc_clz64(x), \
		unsigned long long:	(unsigned)__stdc_clz64(x))

#define stdc_leading_ones(x) \
	_Generic((x), \
		unsigned char:		(unsigned)__stdc_clz8((unsigned char)~(x)), \
		unsigned short:		(unsigned)__stdc_clz16((unsigned short)~(x)), \
		unsigned int:		(unsigned)__stdc_clz32(~(x)), \
		unsigned long:		(unsigned)__stdc_clz64(~(x)), \
		unsigned long long:	(unsigned)__stdc_clz64(~(x)))

#define stdc_trailing_zeros(x) \
	_Generic((x), \
		unsigned char:		(unsigned)__stdc_ctz8((unsigned char)(x)), \
		unsigned short:		(unsigned)__stdc_ctz16((unsigned short)(x)), \
		unsigned int:		(unsigned)__stdc_ctz32(x), \
		unsigned long:		(unsigned)__stdc_ctz64(x), \
		unsigned long long:	(unsigned)__stdc_ctz64(x))

#define stdc_trailing_ones(x) \
	_Generic((x), \
		unsigned char:		(unsigned)__stdc_ctz8((unsigned char)~(x)), \
		unsigned short:		(unsigned)__stdc_ctz16((unsigned short)~(x)), \
		unsigned int:		(unsigned)__stdc_ctz32(~(x)), \
		unsigned long:		(unsigned)__stdc_ctz64(~(x)), \
		unsigned long long:	(unsigned)__stdc_ctz64(~(x)))

/* ---- 7.18.2.2: first leading/trailing zero/one bit position ---- */

#define stdc_first_leading_zero(x)	(stdc_leading_zeros(x) + 1u)
#define stdc_first_leading_one(x)	(stdc_leading_ones(x) + 1u)
#define stdc_first_trailing_zero(x)	(stdc_trailing_zeros(x) + 1u)
#define stdc_first_trailing_one(x)	(stdc_trailing_ones(x) + 1u)

/* ---- 7.18.2.3: count of zero / one bits ---- */

#define stdc_count_ones(x) \
	_Generic((x), \
		unsigned char:		(unsigned)__stdc_popcount32((unsigned)(x)), \
		unsigned short:		(unsigned)__stdc_popcount32((unsigned)(x)), \
		unsigned int:		(unsigned)__stdc_popcount32(x), \
		unsigned long:		(unsigned)__stdc_popcount64(x), \
		unsigned long long:	(unsigned)__stdc_popcount64(x))

#define stdc_count_zeros(x)	(stdc_bit_width(x) - stdc_count_ones(x))

/* ---- 7.18.2.4: single-bit test ---- */

#define stdc_has_single_bit(x)	(((x) != 0u) && (((x) & ((x) - 1u)) == 0u))

/* ---- 7.18.2.5: bit width ---- */

#define stdc_bit_width(x) \
	_Generic((x), \
		unsigned char:		(unsigned)(sizeof(x) * 8u) - stdc_leading_zeros(x), \
		unsigned short:		(unsigned)(sizeof(x) * 8u) - stdc_leading_zeros(x), \
		unsigned int:		(unsigned)(sizeof(x) * 8u) - stdc_leading_zeros(x), \
		unsigned long:		(unsigned)(sizeof(x) * 8u) - stdc_leading_zeros(x), \
		unsigned long long:	(unsigned)(sizeof(x) * 8u) - stdc_leading_zeros(x))

/* ---- 7.18.2.6: bit floor / ceil ---- */

#define stdc_bit_floor(x) \
	_Generic((x), \
		unsigned char:		(unsigned char)((x) == 0u ? 0u : (1u << (stdc_bit_width(x) - 1u))), \
		unsigned short:		(unsigned short)((x) == 0u ? 0u : (1u << (stdc_bit_width(x) - 1u))), \
		unsigned int:		((x) == 0u ? 0u : (1u << (stdc_bit_width(x) - 1u))), \
		unsigned long:		((x) == 0u ? 0ul : (1ul << (stdc_bit_width(x) - 1u))), \
		unsigned long long:	((x) == 0u ? 0ull : (1ull << (stdc_bit_width(x) - 1u))))

#define stdc_bit_ceil(x) \
	_Generic((x), \
		unsigned char:		(unsigned char)((x) <= 1u ? 1u : \
			(1u << (stdc_bit_width((unsigned char)((x) - 1u))))), \
		unsigned short:		(unsigned short)((x) <= 1u ? 1u : \
			(1u << (stdc_bit_width((unsigned short)((x) - 1u))))), \
		unsigned int:		((x) <= 1u ? 1u : (1u << (stdc_bit_width((x) - 1u)))), \
		unsigned long:		((x) <= 1ul ? 1ul : (1ul << (stdc_bit_width((x) - 1ul)))), \
		unsigned long long:	((x) <= 1ull ? 1ull : (1ull << (stdc_bit_width((x) - 1ull)))))

#endif /* MEUOS_STDBIT_H */