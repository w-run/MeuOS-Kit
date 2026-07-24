/* aarch64/apply.c — AArch64 relocation application.
 *
 * Implements the relocation types that mcc emits and the
 * assembler produces for AArch64.
 */

#include <stdint.h>
#include <stddef.h>

/* Relocation type constants (from reloc.c header) */
#define R_AARCH64_ABS64       257
#define R_AARCH64_ABS32       258
#define R_AARCH64_CALL26      283
#define R_AARCH64_JUMP26      282
#define R_AARCH64_ADR_PREL_PG_HI21  275
#define R_AARCH64_ADR_PREL_LO21     274
#define R_AARCH64_CONDBR19          279
#define R_AARCH64_MOVW_UABS_G0_NC   260

/* Apply a single AArch64 relocation.
 *
 *   type   — ELF relocation type
 *   loc    — pointer to the 4-byte instruction (or 8-byte data) to patch
 *   S      — symbol value (resolved address)
 *   A      — addend (from RELA)
 *   P      — place (address of the relocation site)
 *
 * Returns 0 on success, -1 on unsupported relocation type.
 */
int
mt_apply_aarch64_reloc(unsigned type, unsigned char *loc,
                       uint64_t S, int64_t A, uint64_t P)
{
	switch (type) {
	case R_AARCH64_ABS64:
		/* S + A → 64-bit write */
		loc[0] = (uint8_t)(S + A);
		loc[1] = (uint8_t)((S + A) >> 8);
		loc[2] = (uint8_t)((S + A) >> 16);
		loc[3] = (uint8_t)((S + A) >> 24);
		loc[4] = (uint8_t)((S + A) >> 32);
		loc[5] = (uint8_t)((S + A) >> 40);
		loc[6] = (uint8_t)((S + A) >> 48);
		loc[7] = (uint8_t)((S + A) >> 56);
		return 0;

	case R_AARCH64_ABS32:
		/* S + A → 32-bit write (truncated) */
		loc[0] = (uint8_t)(S + A);
		loc[1] = (uint8_t)((S + A) >> 8);
		loc[2] = (uint8_t)((S + A) >> 16);
		loc[3] = (uint8_t)((S + A) >> 24);
		return 0;

	case R_AARCH64_CALL26:
	case R_AARCH64_JUMP26: {
		/* (S + A - P) / 4 → 26-bit signed immediate in BL/B instruction
		 * Instruction format: [31:26] = 000101 (B) or 100101 (BL)
		 *                     [25:0]  = imm26 * 4 */
		int64_t delta = (int64_t)(S + A - P);
		if (delta < -(1LL << 27) || delta >= (1LL << 27))
			return -1; /* overflow */
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		uint32_t imm26 = (uint32_t)((delta >> 2) & 0x03FFFFFF);
		insn = (insn & 0xFC000000) | imm26;
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case R_AARCH64_ADR_PREL_PG_HI21: {
		/* Page(S + A) - Page(P) → 21-bit signed immediate in ADRP
		 * ADRP: immlo[30:29], immhi[23:5], Rd[4:0] */
		uint64_t page_s = (S + A) & ~0xFFFULL;
		uint64_t page_p = P & ~0xFFFULL;
		int64_t delta = (int64_t)(page_s - page_p);
		if (delta < -(1LL << 32) || delta >= (1LL << 32))
			return -1;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		uint32_t imm = (uint32_t)((delta >> 12) & 0x1FFFFF);
		uint32_t immlo = imm & 3;
		uint32_t immhi = imm >> 2;
		insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case R_AARCH64_ADR_PREL_LO21: {
		/* (S + A - P) & 0xFFF → 12-bit signed offset in ADD/LDR/STR
		 * Or ADR: immhi[23:5], immlo[30:29] */
		int64_t delta = (int64_t)(S + A - P);
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		uint32_t imm = (uint32_t)(delta & 0x1FFFFF);
		uint32_t immlo = imm & 3;
		uint32_t immhi = imm >> 2;
		insn = (insn & 0x9F00001F) | (immlo << 29) | (immhi << 5);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case R_AARCH64_CONDBR19: {
		/* (S + A - P) / 4 → 19-bit signed immediate in B.cond / CBZ / CBNZ
		 * Instruction: [31] = cond/NOT, [30:25] = 010101, [24:5] = imm19, [4:0] = rt/cond */
		int64_t delta = (int64_t)(S + A - P);
		if (delta < -(1LL << 20) || delta >= (1LL << 20))
			return -1;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		uint32_t imm19 = (uint32_t)((delta >> 2) & 0x7FFFF);
		insn = (insn & 0xFF00001F) | (imm19 << 5);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case R_AARCH64_MOVW_UABS_G0_NC: {
		/* (S + A) & 0xFFFF → 16-bit immediate in MOVZ/MOVK
		 * Instruction: [31:23] = sf(1) opc(10) hw, [22:21] = 00, [20:5] = imm16, [4:0] = Rd */
		uint32_t val = (uint32_t)(S + A) & 0xFFFF;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFE0001F) | (val << 5);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	default:
		return -1;
	}
}
