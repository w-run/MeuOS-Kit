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
static inline uint16_t read16(const unsigned char *p) {
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline void write16(unsigned char *p, uint16_t v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

/* Apply a single ARM relocation.
 * Returns 0 on success, -1 on unsupported. */
int
mt_apply_arm_reloc(unsigned type, unsigned char *loc,
                  uint64_t S, int64_t A, uint64_t P)
{
	uint32_t insn;
	uint16_t imm16;
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

	case 96: /* R_ARM_GOT_PREL: GOT + A - P */
		write32(loc, (uint32_t)(S + A - P));
		return 0;

	case 27: /* R_ARM_PLT32: PLT + A - P */
		write32(loc, (uint32_t)(S + A - P));
		return 0;

	case 26: /* R_ARM_GOT32: GOT entry offset */
		write32(loc, (uint32_t)(S + A));
		return 0;

	case 4: /* R_ARM_LDR_PC_G0: S + A - P (LDR literal) */
		write32(loc, (uint32_t)(S + A - P));
		return 0;

	case 32: /* R_ARM_ALU_PCREL_0: (S + A - P) for ADR */
		{
			insn = read32(loc);
			offset = (int64_t)(S + A - P);
			if (offset < 0 || offset > 4095) return -1;
			insn = (insn & 0xFFF00000) | (uint32_t)offset;
			write32(loc, insn);
			return 0;
		}

	case 104: /* R_ARM_TLS_GD32: GOT + A - P (TLS GD) */
		write32(loc, (uint32_t)(S + A - P));
		return 0;

	case 106: /* R_ARM_TLS_LDO32: S + A - tp (TLS LDO) */
		write32(loc, (uint32_t)(S + A));
		return 0;

	/* ---- Dynamic linking relocations (preserved for ld.so) ---- */
	case 20: /* R_ARM_COPY — copy relocation; ld.so copies data */
		return 0;

	case 21: /* R_ARM_GLOB_DAT — set GOT entry to symbol value; ld.so */
		return 0;

	case 22: /* R_ARM_JUMP_SLOT — PLT jump slot; ld.so resolves symbol */
		return 0;

	case 23: /* R_ARM_RELATIVE — base + addend; ld.so adds load bias */
		return 0;

	/* ---- Thumb relocations ---- */

	case 10: /* R_ARM_THM_CALL: Thumb BL/BLX (S + A - P) */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint32_t S_bit, I1, I2, imm10h, imm10l;
			uint32_t J1, J2;
			int64_t imm;

			imm = (S + A - P) >> 1; /* 24-bit signed halfword offset */
			if (imm < -8388608LL || imm > 8388607LL)
				return -1; /* out of range */
			S_bit = ((uint32_t)imm >> 23) & 1;
			I1 = ((uint32_t)imm >> 22) & 1;
			I2 = ((uint32_t)imm >> 21) & 1;
			imm10h = ((uint32_t)imm >> 12) & 0x3FF;
			imm10l = ((uint32_t)imm >> 2) & 0x3FF;
			J1 = !(I1 ^ S_bit);
			J2 = !(I2 ^ S_bit);

			/* Upper: 1111 0 S 1 1 1 1 1 1 1 0 1 0 */
			upper = (upper & 0xF800) | (S_bit << 10) | imm10h;
			/* Lower: 1 1 J1 1 J2 1 1 0 1 imm10l */
			lower = (lower & 0xD000) | (J1 << 13) | (J2 << 11) | (1 << 10) | (imm10l << 1);
			/* bits [10:1] = imm10l, bit [0] = 0 for BL, preserved via D000 */
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	case 30: /* R_ARM_THM_JUMP24: Thumb B.W (S + A - P) */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint32_t S_bit, I1, I2, imm10h, imm10l;
			uint32_t J1, J2;
			int64_t imm;

			imm = (S + A - P) >> 1; /* 24-bit signed halfword offset */
			if (imm < -8388608LL || imm > 8388607LL)
				return -1; /* out of range */
			S_bit = ((uint32_t)imm >> 23) & 1;
			I1 = ((uint32_t)imm >> 22) & 1;
			I2 = ((uint32_t)imm >> 21) & 1;
			imm10h = ((uint32_t)imm >> 12) & 0x3FF;
			imm10l = ((uint32_t)imm >> 2) & 0x3FF;
			J1 = !(I1 ^ S_bit);
			J2 = !(I2 ^ S_bit);

			/* Upper: 1111 0 S 0 0 1 0 imm10h (B.W encoding) */
			upper = (upper & 0xF800) | (S_bit << 10) | imm10h;
			/* Lower: 1 0 J1 1 J2 0 1 0 imm10l (B.W encoding) */
			lower = (lower & 0xD000) | (J1 << 13) | (J2 << 11) | (1 << 10) | (imm10l << 1);
			/* bit [10] is IT/AL hint; preserve via mask */
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	case 47: /* R_ARM_THM_MOVW_ABS_NC: lower 16 of S+A */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint16_t imm = (uint16_t)(S + A);
			unsigned i = (imm >> 11) & 1;
			unsigned imm4 = (imm >> 12) & 0xF;  /* bits 15:12 */
			unsigned imm3 = (imm >> 8) & 0x7;   /* bits 10:8 */
			unsigned imm8 = imm & 0xFF;          /* bits 7:0 */

			/* Upper: 1111 0 i 1 0 0 1 0 0 imm4 */
			upper = (upper & 0xFBFF) | (i << 10);
			upper = (upper & 0xFFF0) | imm4;
			/* Lower: 0 1 1 1 1 0 Rd imm3 1 0 imm8 */
			/* We extract Rd from the existing instruction */
			lower = (lower & 0xF800) | ((lower >> 8) & 0xF) | (imm3 << 5) | imm8;
			/* Preserve Rd and bit 4 (1 for MOVW) */
			lower = (lower & 0xF8F0) | ((lower >> 8) & 0x7) << 8 | (imm3 << 5) | imm8;
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	case 48: /* R_ARM_THM_MOVT_ABS: upper 16 of S+A */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint16_t imm = (uint16_t)((S + A) >> 16);
			unsigned i = (imm >> 11) & 1;
			unsigned imm4 = (imm >> 12) & 0xF;
			unsigned imm3 = (imm >> 8) & 0x7;
			unsigned imm8 = imm & 0xFF;

			/* Upper: 1111 0 i 1 0 1 1 0 0 imm4 */
			upper = (upper & 0xFBFF) | (i << 10);
			upper = (upper & 0xFFF0) | imm4;
			lower = (lower & 0xF8F0) | ((lower >> 8) & 0x7) << 8 | (imm3 << 5) | imm8;
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	case 49: /* R_ARM_THM_MOVW_PREL_NC: lower 16 of S+A-P */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint16_t imm = (uint16_t)(S + A - P);
			unsigned i = (imm >> 11) & 1;
			unsigned imm4 = (imm >> 12) & 0xF;
			unsigned imm3 = (imm >> 8) & 0x7;
			unsigned imm8 = imm & 0xFF;

			upper = (upper & 0xFBFF) | (i << 10);
			upper = (upper & 0xFFF0) | imm4;
			lower = (lower & 0xF8F0) | ((lower >> 8) & 0x7) << 8 | (imm3 << 5) | imm8;
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	case 50: /* R_ARM_THM_MOVT_PREL: upper 16 of S+A-P */
		{
			uint16_t upper = read16(loc);
			uint16_t lower = read16(loc + 2);
			uint16_t imm = (uint16_t)((S + A - P) >> 16);
			unsigned i = (imm >> 11) & 1;
			unsigned imm4 = (imm >> 12) & 0xF;
			unsigned imm3 = (imm >> 8) & 0x7;
			unsigned imm8 = imm & 0xFF;

			upper = (upper & 0xFBFF) | (i << 10);
			upper = (upper & 0xFFF0) | imm4;
			lower = (lower & 0xF8F0) | ((lower >> 8) & 0x7) << 8 | (imm3 << 5) | imm8;
			write16(loc, upper);
			write16(loc + 2, lower);
			return 0;
		}

	default:
		return -1;
	}
}
