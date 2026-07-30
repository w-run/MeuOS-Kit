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
#define R_AARCH64_PREL32      261
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

	case R_AARCH64_PREL32: {
		/* (S + A - P) → 32-bit signed relative offset */
		int64_t delta = (int64_t)(S + A - P);
		uint32_t v = (uint32_t)(delta);
		loc[0] = (uint8_t)(v);
		loc[1] = (uint8_t)(v >> 8);
		loc[2] = (uint8_t)(v >> 16);
		loc[3] = (uint8_t)(v >> 24);
		return 0;
	}

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

	case 286: { /* R_AARCH64_LDST64_ABS_LO12_NC */
		/* Low 12 bits of (S + A) for 64-bit LDR/STR scaled addressing.
		 * The value is placed in bits [21:10] of the instruction. */
		uint64_t val = (uint64_t)(S + A);
		uint32_t imm12 = (uint32_t)((val >> 3) & 0xFFF);
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFC003FF) | (imm12 << 10);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case 277: { /* R_AARCH64_ADD_ABS_LO12_NC */
		/* Low 12 bits of (S + A) for ADD immediate.
		 * Value placed in bits [21:10] of ADD (immediate) instruction. */
		uint32_t imm12 = (uint32_t)(S + A) & 0xFFF;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFC003FF) | (imm12 << 10);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case 311: { /* R_AARCH64_ADR_GOT_PAGE — ADRP to GOT page */
		uint64_t page_got = (S + A) & ~0xFFFULL;
		uint64_t page_p = P & ~0xFFFULL;
		int64_t delta = (int64_t)(page_got - page_p);
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

	case 312: { /* R_AARCH64_LD64_GOT_LO12_NC — LDR from GOT entry */
		uint32_t imm12 = (uint32_t)((S + A) >> 3) & 0xFFF;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFC003FF) | (imm12 << 10);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case 549: { /* R_AARCH64_TLSLE_ADD_TPREL_HI12 */
		uint32_t imm12 = (uint32_t)((S + A) >> 12) & 0xFFF;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFC003FF) | (imm12 << 10);
		loc[0] = (uint8_t)(insn);
		loc[1] = (uint8_t)(insn >> 8);
		loc[2] = (uint8_t)(insn >> 16);
		loc[3] = (uint8_t)(insn >> 24);
		return 0;
	}

	case 551: { /* R_AARCH64_TLSLE_ADD_TPREL_LO12_NC (actual GNU ABI) */
		uint32_t imm12 = (uint32_t)(S + A) & 0xFFF;
		uint32_t insn = (uint32_t)loc[0] | ((uint32_t)loc[1] << 8) |
		                ((uint32_t)loc[2] << 16) | ((uint32_t)loc[3] << 24);
		insn = (insn & 0xFFC003FF) | (imm12 << 10);
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

/* Write a 32-bit relative value at the given location.
 * Used for R_AARCH64_PREL32 in .eh_frame sections. */
int
mt_apply_aarch64_prel32(unsigned char *loc, uint64_t value)
{
	uint32_t v = (uint32_t)(int32_t)(int64_t)value;
	loc[0] = (uint8_t)(v);
	loc[1] = (uint8_t)(v >> 8);
	loc[2] = (uint8_t)(v >> 16);
	loc[3] = (uint8_t)(v >> 24);
	return 0;
}
