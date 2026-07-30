/* apply.c — RISC-V relocation application.

 * Applies RISC-V relocations against a linked output section.
 * Called by the linker after layout to patch the final binary content. */

#include <stdint.h>

static uint32_t
read32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

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

/* Set bits [hi:lo] of *p to val (val must be already shifted) */
static void
set_bits(unsigned char *p, int hi, int lo, uint32_t val)
{
	uint32_t mask = ~(((1U << (hi - lo + 1)) - 1) << lo);
	uint32_t cur = read32(p);
	cur = (cur & mask) | (val & ~mask);
	write32(p, cur);
}

/* Apply a single RISC-V relocation.
 *
 * reloc_type:  R_RISCV_* constant
 * place:       4-byte aligned location to patch (in-place)
 * S:           symbol value (final linked address)
 * A:           addend (from the relocation entry)
 * P:           position of the place (the offset being relocated)
 *
 * Returns 0 on success, -1 on unsupported relocation type. */
int
riscv64_apply_reloc(unsigned reloc_type, unsigned char *place,
                    uint64_t S, int64_t A, uint64_t P)
{
	int64_t delta;

	switch (reloc_type) {
	case 0: /* R_RISCV_NONE */
	case 51: /* R_RISCV_RELAX — linker relaxation marker, skip */
		return 0;

	case 2: /* R_RISCV_64: S + A (64-bit write) */
		write64(place, S + (uint64_t)A);
		return 0;

	case 1: /* R_RISCV_32: S + A (32-bit write) */
		write32(place, (uint32_t)(S + (uint64_t)A));
		return 0;

	case 23: /* R_RISCV_PCREL_HI20: (S + A - P + 0x800) >> 12 → AUIPC/LUI imm20 */
		delta = (int64_t)(S + (uint64_t)A - P);
		set_bits(place, 31, 12, (uint32_t)((delta + 0x800) >> 12) << 12);
		return 0;

	case 24: /* R_RISCV_LO12_I: (S + A) & 0xFFF → I-type load immediate */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 31, 20, (uint32_t)(delta & 0xFFF) << 20);
		return 0;

	case 25: /* R_RISCV_LO12_S: (S + A) & 0xFFF → S-type store immediate */
		delta = (int64_t)(S + (uint64_t)A);
		{
			uint32_t val = (uint32_t)(delta & 0xFFF);
			uint32_t lo = val & 0x1F;
			uint32_t hi = (val >> 5) & 0x7F;
			set_bits(place, 11, 7, lo << 7);
			set_bits(place, 31, 25, hi << 25);
		}
		return 0;

	case 26: /* R_RISCV_HI20: (S + A + 0x800) >> 12 */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 31, 12, (uint32_t)((delta + 0x800) >> 12) << 12);
		return 0;

	case 27: /* R_RISCV_LO12_I: same as 24 */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 31, 20, (uint32_t)(delta & 0xFFF) << 20);
		return 0;

	case 28: /* R_RISCV_LO12_S: same as 25 */
		delta = (int64_t)(S + (uint64_t)A);
		{
			uint32_t val = (uint32_t)(delta & 0xFFF);
			uint32_t lo = val & 0x1F;
			uint32_t hi = (val >> 5) & 0x7F;
			set_bits(place, 11, 7, lo << 7);
			set_bits(place, 31, 25, hi << 25);
		}
		return 0;

	case 16: /* R_RISCV_BRANCH: B-type offset */
		delta = (int64_t)(S + (uint64_t)A - P);
		{
			uint32_t abs = (uint32_t)(delta & 0x1FFF);
			set_bits(place, 31, 31, (abs >> 12) << 31);
			set_bits(place, 7, 7, ((abs >> 11) & 1) << 7);
			set_bits(place, 30, 25, ((abs >> 5) & 0x3F) << 25);
			set_bits(place, 11, 8, ((abs >> 1) & 0xF) << 8);
		}
		return 0;

	case 17: /* R_RISCV_JAL: J-type offset */
		delta = (int64_t)(S + (uint64_t)A - P);
		{
			uint32_t abs = (uint32_t)(delta & 0x1FFFFF);
			set_bits(place, 31, 31, (abs >> 20) << 31);
			set_bits(place, 30, 21, ((abs >> 1) & 0x3FF) << 21);
			set_bits(place, 20, 20, ((abs >> 11) & 1) << 20);
			set_bits(place, 19, 12, ((abs >> 12) & 0xFF) << 12);
		}
		return 0;

	case 18: /* R_RISCV_CALL: auipc + jalr pair */
	case 19: /* R_RISCV_CALL_PLT: same as CALL for now */
	{
		/* The pair: auipc rd, imm20  ;  jalr rd, rd, imm12 */
		uint64_t target = S + (uint64_t)A;
		int64_t off = (int64_t)(target - P);
		/* auipc sets rd to PC + (imm20 << 12) */
		uint32_t hi20 = (uint32_t)((off + 0x800) >> 12) & 0xFFFFF;
		uint32_t lo12 = (uint32_t)(off & 0xFFF);
		set_bits(place, 31, 12, hi20 << 12);
		set_bits(place + 4, 31, 20, lo12 << 20);
		return 0;
	}

	case 38: /* R_RISCV_TPREL_ADD: rd = rd + tp (zero contribution in static link) */
		return 0;

	case 39: /* R_RISCV_TPREL_LO12_I: (S + A) & 0xFFF → I-type immediate */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 31, 20, (uint32_t)(delta & 0xFFF) << 20);
		return 0;

	case 40: /* R_RISCV_TPREL_LO12_S: (S + A) & 0xFFF → S-type store immediate */
		delta = (int64_t)(S + (uint64_t)A);
		{
			uint32_t val = (uint32_t)(delta & 0xFFF);
			uint32_t lo = val & 0x1F;
			uint32_t hi = (val >> 5) & 0x7F;
			set_bits(place, 11, 7, lo << 7);
			set_bits(place, 31, 25, hi << 25);
		}
		return 0;

	case 41: /* R_RISCV_TPREL_HI20: (S + A + 0x800) >> 12 → LUI imm20 */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 31, 12, (uint32_t)((delta + 0x800) >> 12) << 12);
		return 0;

	default:
		return -1; /* unsupported */
	}
}
