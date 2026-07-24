/* apply.c — LoongArch relocation application. */
#include <stdint.h>

static void
write32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static void
write64(unsigned char *p, uint64_t v)
{
	write32(p, (uint32_t)v);
	write32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
set_bits(unsigned char *p, int hi, int lo, uint32_t val)
{
	uint32_t mask = ~(((1U << (hi - lo + 1)) - 1) << lo);
	uint32_t cur = read32(p);
	cur = (cur & mask) | (val & ~mask);
	write32(p, cur);
}

/* Apply a single LoongArch relocation.
 *
 * reloc_type:  R_LARCH_* constant
 * place:       4-byte aligned location to patch (in-place)
 * S:           symbol value (final linked address)
 * A:           addend (from the relocation entry)
 * P:           position of the place (the offset being relocated)
 *
 * Returns 0 on success, -1 on unsupported relocation type. */
int
la64_apply_reloc(unsigned reloc_type, unsigned char *place,
                 uint64_t S, int64_t A, uint64_t P)
{
	int64_t delta;

	switch (reloc_type) {
	case 0: /* R_LARCH_NONE */
		return 0;

	case 2: /* R_LARCH_64: S + A (64-bit) */
		write64(place, S + (uint64_t)A);
		return 0;

	case 1: /* R_LARCH_32: S + A (32-bit) */
		write32(place, (uint32_t)(S + (uint64_t)A));
		return 0;

	case 5: /* R_LARCH_B26: Branch 26-bit offset (bl/b) */
		delta = (int64_t)(S + (uint64_t)A - P);
		set_bits(place, 31, 10, (uint32_t)((delta >> 2) & 0x3FFFFF) << 10);
		return 0;

	case 20: /* R_LARCH_PCALA_HI20: Page(S+A-P)>>12 → 20-bit in PCALAU12I/LU12I.W */
		delta = (int64_t)(S + (uint64_t)A - P);
		set_bits(place, 31, 12, (uint32_t)((delta + 0x800) >> 12) << 12);
		return 0;

	case 21: /* R_LARCH_PCALA_LO12: (S+A) & 0xFFF → 12-bit in ADD/LD/ST immediate */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 21, 10, (uint32_t)(delta & 0xFFF) << 10);
		return 0;

	case 47: /* R_LARCH_TLS_LE_HI20: TPOFF(S+A+0x800)>>12 */
		/* For static TLS, the offset is from TP */
		set_bits(place, 31, 12, (uint32_t)((S + (uint64_t)A + 0x800) >> 12) << 12);
		return 0;

	case 49: /* R_LARCH_TLS_LE_ADD_LO12: low 12 bits of TLS LE offset */
		set_bits(place, 21, 10, (uint32_t)((S + (uint64_t)A) & 0xFFF) << 10);
		return 0;

	default:
		return -1;
	}
}
