/* apply.c — ARM (armv7) relocation application.
 *
 * Applies ARM ELF32 relocations against a linked output section.
 * Called by the linker after layout to patch the final binary content. */
#include <stdint.h>

static inline uint32_t read32(const unsigned char *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void write32(unsigned char *p, uint32_t v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* Apply a single ARM relocation.
 * Returns 0 on success, -1 on unsupported. */
int
armv7_apply_reloc(unsigned type, unsigned char *loc,
                  uint64_t S, int64_t A, uint64_t P)
{
	uint32_t insn, imm16;
	int64_t offset;

	switch (type) {
	case 0: /* R_ARM_NONE */
		return 0;

	case 1: /* R_ARM_PC24: (S + A - P) >> 2 */
	case 28:/* R_ARM_CALL */
	case 29:/* R_ARM_JUMP24 */
		insn = read32(loc);
		offset = (int64_t)(S + A - P);
		if ((offset & 3) != 0)
			return -1; /* misaligned */
		offset >>= 2;
		if (offset < -8388608LL || offset > 8388607LL)
			return -1; /* out of range */
		insn = (insn & 0xFF000000) | (offset & 0x00FFFFFF);
		write32(loc, insn);
		return 0;

	case 2: /* R_ARM_ABS32: S + A */
		write32(loc, (uint32_t)(S + A));
		return 0;

	case 3: /* R_ARM_REL32: S + A - P */
		write32(loc, (uint32_t)(S + A - P));
		return 0;

	case 43: /* R_ARM_MOVW_ABS_NC: lower 16 of S+A */
		insn = read32(loc);
		imm16 = (uint16_t)(S + A);
		insn = (insn & 0xFFF0F000) | ((insn & 0xF0000) == 0 ? 0 : 0)
		     | ((imm16 & 0xF000) << 4) | (imm16 & 0x0FFF);
		/* MOVW: bits [19:16] = imm[15:12], bits [11:0] = imm[11:0] */
		insn = (insn & 0xFFF0F000) | ((uint32_t)((imm16 >> 12) & 0xF) << 16) | (imm16 & 0x0FFF);
		write32(loc, insn);
		return 0;

	case 44: /* R_ARM_MOVT_ABS: upper 16 of S+A */
		insn = read32(loc);
		imm16 = (uint16_t)((S + A) >> 16);
		insn = (insn & 0xFFF0F000) | ((uint32_t)((imm16 >> 12) & 0xF) << 16) | (imm16 & 0x0FFF);
		write32(loc, insn);
		return 0;

	case 45: /* R_ARM_MOVW_PREL_NC: lower 16 of S+A-P */
		insn = read32(loc);
		imm16 = (uint16_t)(S + A - P);
		insn = (insn & 0xFFF0F000) | ((uint32_t)((imm16 >> 12) & 0xF) << 16) | (imm16 & 0x0FFF);
		write32(loc, insn);
		return 0;

	case 46: /* R_ARM_MOVT_PREL: upper 16 of S+A-P */
		insn = read32(loc);
		imm16 = (uint16_t)((S + A - P) >> 16);
		insn = (insn & 0xFFF0F000) | ((uint32_t)((imm16 >> 12) & 0xF) << 16) | (imm16 & 0x0FFF);
		write32(loc, insn);
		return 0;

	case 107: /* R_ARM_TLS_LE32: S + A - tp (TLS local exec) */
		/* For static TLS, the linker computes the final offset
		 * from the thread pointer.  S is the TLS symbol address,
		 * tp is known at link time for static binaries. */
		write32(loc, (uint32_t)(S + A));
		return 0;

	case 96: /* R_ARM_GOT_PREL */
		return -1; /* not yet implemented */

	default:
		return -1;
	}
}
