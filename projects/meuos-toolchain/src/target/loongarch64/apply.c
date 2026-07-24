/* apply.c — LoongArch relocation application.
 *
 * Relocation type numbers match /usr/include/elf.h (psABI v2.x).
 *
 * LOONGARCH INSTRUCTION ENCODING NOTES:
 *
 * 1RI20 format (pcalau12i / lu12i.w / lu32i.d):
 *   base = opcode_dependent_byte << 24  (0x1A/pcalau12i, 0x14/lu12i.w, etc.)
 *   bits [4:0]  = rd
 *   bits [24:5] = si20 (20-bit signed immediate, shifted by 5)
 *   insn = base | rd | (si20 << 5)
 *
 * 2RI12 format (addi.d / ld.d / st.d / add.w):
 *   bits [4:0]  = rd
 *   bits [9:5]  = rj
 *   bits [21:10] = si12 (12-bit signed immediate)
 *   Use set_bits(place, 21, 10, (si12 & 0xFFF) << 10)
 *
 * B26 / B16 branch format (bl / b):
 *   bits [9:0]  = opcode
 *   bits [31:10] = (offset >> 2) & 0x3FFFFF (26-bit branch offset in words)
 *   Use set_bits(place, 31, 10, ...) */

#include <stdint.h>
#include <string.h>

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

	/* ==== PC-relative branch: R_LARCH_B26 (66) ====
	 * LA64 B26 format: 26-bit word-offset in bits [25:0] via a
	 * LEFT-ROTATE-BY-10 within 26 bits:
	 *   instr_bits[i] = offset_words[(i - 10 + 26) % 26]
	 * Equivalently:
	 *   encoded = ((offset_words << 10) | (offset_words >> 16)) & 0x3FFFFFF
	 * The opcode is at bits [31:26] (bl=0x15, b=0x14).
	 * Use set_bits with hi=25, lo=0 — the "register" bits at [4:0]
	 * ARE the upper offset bits by LA64 encoding design. */
	case 66: /* R_LARCH_B26 */
		delta = (int64_t)(S + (uint64_t)A - P);
		{
			uint32_t w = (uint32_t)((uint64_t)(delta >> 2) & 0x3FFFFFF);
			uint32_t enc = (uint32_t)((((uint64_t)w << 10) |
			                   ((uint64_t)w >> 16)) & 0x3FFFFFF);
			set_bits(place, 25, 0, enc);
		}
		return 0;

	/* ==== PC-relative address: R_LARCH_PCALA_HI20 (71) ====
	 * Page-relative: Page(S+A) - Page(P), upper 20 bits.
	 *   imm20 = (S+A)>>12 - P>>12 = (S+A - (P & ~0xFFF)) >> 12
	 * Instruction: pcalau12i (1RI20 format)
	 *   base = 0x1A000000, insn = base | rd | (imm20 << 5) */
	case 71: /* R_LARCH_PCALA_HI20 */
		write32(place, 0x1A000000 | (read32(place) & 0x1F) |
		        ((uint32_t)(((S + (uint64_t)A) >> 12) - (P >> 12)) << 5));
		return 0;

	/* ==== PC-relative offset: R_LARCH_PCALA_LO12 (72) ====
	 * Lower 12 bits: (S+A) & 0xFFF.
	 * Used with ADD/load/store immediate:
	 *   insn[21:10] = (S+A) & 0xFFF  (12-bit signed immediate) */
	case 72: /* R_LARCH_PCALA_LO12 */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 21, 10, (uint32_t)(delta & 0xFFF) << 10);
		return 0;

	/* ==== GOT page address: R_LARCH_GOT_PC_HI20 (75) ====
	 * pcalau12i: Page(GOT) - Page(P), upper 20 bits.
	 *   insn = 0x1A000000 | rd | (((S>>12)-(P>>12)) << 5) */
	case 75: /* R_LARCH_GOT_PC_HI20 */
		write32(place, 0x1A000000 | (read32(place) & 0x1F) |
		        ((uint32_t)((S >> 12) - (P >> 12)) << 5));
		return 0;

	/* ==== GOT page offset: R_LARCH_GOT_PC_LO12 (76) ====
	 * (GOT_ENTRY) & 0xFFF, encoded as 12-bit immediate. */
	case 76: /* R_LARCH_GOT_PC_LO12 */
		delta = (int64_t)(S + (uint64_t)A);
		set_bits(place, 21, 10, (uint32_t)(delta & 0xFFF) << 10);
		return 0;

	/* ==== TLS LE HI20: R_LARCH_TLS_LE_HI20 (83) ====
	 * TPOFF(S+A+0x800)>>12 — upper 20 bits of TLS offset from TP.
	 * Instruction: lu12i.w (1RI20, absolute addressing)
	 *   base = 0x14000000, imm20 = S+A >> 12 (no page rounding needed) */
	case 83: /* R_LARCH_TLS_LE_HI20 */
		write32(place, 0x14000000 | (read32(place) & 0x1F) |
		        ((uint32_t)((S + (uint64_t)A + 0x800) >> 12) << 5));
		return 0;

	/* ==== TLS LE LO12: R_LARCH_TLS_LE_LO12 (84) ====
	 * (S+A) & 0xFFF — lower 12 bits of TLS offset from TP.
	 * insn[21:10] = (S+A) & 0xFFF */
	case 84: /* R_LARCH_TLS_LE_LO12 */
		set_bits(place, 21, 10, (uint32_t)((S + (uint64_t)A) & 0xFFF) << 10);
		return 0;

	/* ==== TLS LE64 HI12: R_LARCH_TLS_LE64_HI12 (86) ====
	 * lu52i.d: bits 63:52 from ((S+A)>>52) & 0xFFF.
	 * insn[21:10] = ((S+A)>>52) & 0xFFF */
	case 86: /* R_LARCH_TLS_LE64_HI12 */
		set_bits(place, 21, 10, (uint32_t)(((S + (uint64_t)A) >> 52) & 0xFFF) << 10);
		return 0;

	/* ==== TLS LE64 LO20: R_LARCH_TLS_LE64_LO20 (85) ====
	 * lu32i.d: bits 51:32 from ((S+A)>>32) & 0xFFFFF.
	 *   base = 0x16000000, insn = 0x16000000 | rd | (imm20 << 5) */
	case 85: /* R_LARCH_TLS_LE64_LO20 */
		write32(place, 0x16000000 | (read32(place) & 0x1F) |
		        ((uint32_t)(((S + (uint64_t)A) >> 32) & 0xFFFFF) << 5));
		return 0;

	/* ==== Marker/skip types ==== */
	case 100: /* R_LARCH_RELAX — linker relaxation marker, no patch */
	case 101: /* R_LARCH_DELETE — instruction deletion marker */
	case 102: /* R_LARCH_ALIGN — alignment directive */
		return 0;

	default:
		return -1;
	}
}
