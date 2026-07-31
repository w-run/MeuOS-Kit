/* loongarch64.c - LoongArch 64-bit disassembler.
 *
 * Implements mt_disasm_loongarch64_one(): decode a single LA64 instruction
 * from a raw byte stream and produce mnemonic + operands string.
 *
 * All instructions are fixed 32-bit.  Decoding matches the encoding in
 * src/target/loongarch64/encode.c.
 *
 * LoongArch instruction formats (LA64 base ISA):
 *   3R:     [31:15]=func17  [14:10]=rk  [9:5]=rj  [4:0]=rd
 *   2RI12:  [31:22]=op10    [21:10]=i12 [9:5]=rj  [4:0]=rd
 *   2RI14:  [31:15]=func17  [14:10]=sa  [9:5]=rj  [4:0]=rd  (shifts)
 *   1RI20:  [31:24]=op8     [23:5]=i20  [4:0]=rd             (lu12i.w etc.)
 *   B26:    [31:26]=op6     [25:0]=i26                       (b/bl)
 *   B16:    [31:26]=op6     [25:10]=i16 [9:5]=rj  [4:0]=rd   (beq etc.)
 *   BJ21:   [31:26]=op6     [25:10]=i16 [9:5]=rj  [4:0]=rd   (jirl)
 *   BEQZ:   [31:24]=op8     [23:10]=i14 [9:5]=rj  [4:0]=0    (beqz/bnez)
 *
 * Register ABI names:
 *   r0=$zero, r1=$ra, r2=$tp, r3=$sp, r4-r11=$a0-$a7,
 *   r12-r20=$t0-$t8, r21=$x, r22=$fp, r23=$s0, r24-r30=$s1-$s7, r31=$s8
 */
#include "mt/disasm.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Register ABI names                                                  */
/* ------------------------------------------------------------------ */

static const char *const la_reg[32] = {
	"$zero", "$ra", "$tp", "$sp",
	"$a0", "$a1", "$a2", "$a3",
	"$a4", "$a5", "$a6", "$a7",
	"$t0", "$t1", "$t2", "$t3",
	"$t4", "$t5", "$t6", "$t7",
	"$t8", "$x", "$fp", "$s0",
	"$s1", "$s2", "$s3", "$s4",
	"$s5", "$s6", "$s7", "$s8"
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static char
hexdigit(unsigned v)
{
	return "0123456789abcdef"[v & 15];
}

static int64_t
sext_u(uint64_t val, int bits)
{
	uint64_t m = 1ULL << (bits - 1);
	uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);

	val &= mask;
	if (val & m)
		val |= ~mask;
	return (int64_t)val;
}

/* ------------------------------------------------------------------ */
/* Instruction fetch (little-endian 32-bit)                            */
/* ------------------------------------------------------------------ */

static int
fetch32(const unsigned char *bytes, size_t size, size_t pos, uint32_t *v)
{
	if (pos + 4 > size)
		return -1;
	*v = (uint32_t)bytes[pos]
	     | ((uint32_t)bytes[pos + 1] << 8)
	     | ((uint32_t)bytes[pos + 2] << 16)
	     | ((uint32_t)bytes[pos + 3] << 24);
	return 0;
}

/* ------------------------------------------------------------------ */
/* bytes_hex output                                                    */
/* ------------------------------------------------------------------ */

static void
fill_hex(const unsigned char *bytes, size_t size, size_t start,
         size_t length, struct mt_disasm_insn *out)
{
	char *p = out->bytes_hex;
	size_t cap = sizeof out->bytes_hex;
	size_t n, i;

	n = length;
	if (start + n > size)
		n = (size > start) ? (size - start) : 0;
	if (n > 15)
		n = 15;
	p[0] = '\0';
	if (n == 0)
		return;
	for (i = 0; i < n; ++i) {
		unsigned byte = bytes[start + i];
		if (i) {
			if (p - out->bytes_hex + 1 < (ptrdiff_t)cap)
				*p++ = ' ';
		}
		if (p - out->bytes_hex + 2 < (ptrdiff_t)cap) {
			*p++ = hexdigit(byte >> 4);
			*p++ = hexdigit(byte & 15);
		}
	}
	*p = '\0';
}

static int
fail(struct mt_disasm_insn *out, uint64_t addr, size_t offset,
     size_t length, const unsigned char *bytes, size_t size)
{
	if (length == 0)
		length = 1;
	out->address = addr;
	out->offset = offset;
	out->length = length;
	out->operands[0] = '\0';
	snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
	fill_hex(bytes, size, offset, length, out);
	return -1;
}

/* ------------------------------------------------------------------ */
/* Field extractors                                                    */
/* ------------------------------------------------------------------ */

static unsigned
rd(uint32_t inst)   { return inst & 0x1F; }

static unsigned
rj(uint32_t inst)   { return (inst >> 5) & 0x1F; }

static unsigned
rk(uint32_t inst)   { return (inst >> 10) & 0x1F; }

static unsigned
imm12(uint32_t inst)  { return (inst >> 10) & 0xFFF; }

static unsigned
shamt5(uint32_t inst) { return (inst >> 10) & 0x1F; }

static unsigned
shamt6(uint32_t inst) { return (inst >> 10) & 0x3F; }

static unsigned
imm20(uint32_t inst)  { return (inst >> 5) & 0xFFFFF; }

static unsigned
func17(uint32_t inst) { return (inst >> 15) & 0x1FFFF; }

static unsigned
op10(uint32_t inst)   { return (inst >> 22) & 0x3FF; }

static unsigned
op6(uint32_t inst)    { return (inst >> 26) & 0x3F; }

static unsigned
op8(uint32_t inst)    { return (inst >> 24) & 0xFF; }

static int64_t
off16(uint32_t inst) { return sext_u((inst >> 10) & 0xFFFF, 16) * 4; }

static int64_t
off26(uint32_t inst) { return sext_u(inst & 0x03FFFFFF, 26) * 4; }

static int64_t
off14(uint32_t inst) { return sext_u((inst >> 10) & 0x3FFF, 14) * 4; }

/* ------------------------------------------------------------------ */
/* Emit result: writes mnemonic/operands/hex into out.                 */
/* ------------------------------------------------------------------ */
static int
emit_result(struct mt_disasm_insn *out, uint64_t addr, size_t offset,
            size_t length, const unsigned char *bytes, size_t size,
            const char *mnemonic, const char *operands)
{
	out->address = addr;
	out->offset = offset;
	out->length = length;
	snprintf(out->mnemonic, sizeof out->mnemonic, "%s", mnemonic);
	snprintf(out->operands, sizeof out->operands, "%s", operands);
	fill_hex(bytes, size, offset, length, out);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Main decoder                                                        */
/* ------------------------------------------------------------------ */

static int
decode_one(const unsigned char *bytes, size_t size, size_t offset,
           uint64_t addr, struct mt_disasm_insn *out)
{
	uint32_t inst;
	unsigned rdd, rjj, rkk;
	int64_t imm;
	char rs[20], rt[20];
	char ops[168];

	if (fetch32(bytes, size, offset, &inst) < 0)
		return fail(out, addr, offset, 0, bytes, size);

	rdd = rd(inst);
	rjj = rj(inst);
	rkk = rk(inst);

	/* ---- JIRL (bits 31:26 = 0x13) ---- */
	if (op6(inst) == 0x13) {
		imm = off16(inst);
		snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
		         la_reg[rdd], la_reg[rjj],
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "jirl", ops);
	}

	/* ---- B26: b (bits 31:26 = 0x14), bl (0x15) ---- */
	if (op6(inst) == 0x14) {
		imm = off26(inst);
		snprintf(ops, sizeof ops, "0x%llx",
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size, "b", ops);
	}
	if (op6(inst) == 0x15) {
		imm = off26(inst);
		snprintf(ops, sizeof ops, "0x%llx",
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size, "bl", ops);
	}

	/* ---- B16: beq/bne/blt/bge/bltu/bgeu (0x16-0x1B) ---- */
	if (op6(inst) >= 0x16 && op6(inst) <= 0x1B) {
		static const char *const br[6] = {
			"beq", "bne", "blt", "bge", "bltu", "bgeu"
		};
		imm = off16(inst);
		snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
		         la_reg[rjj], la_reg[rdd],
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   br[op6(inst) - 0x16], ops);
	}

	/* ---- beqz (0x40) / bnez (0x44) ---- */
	if (op8(inst) == 0x40) {
		imm = off14(inst);
		snprintf(ops, sizeof ops, "%s, 0x%llx", la_reg[rjj],
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size, "beqz", ops);
	}
	if (op8(inst) == 0x44) {
		imm = off14(inst);
		snprintf(ops, sizeof ops, "%s, 0x%llx", la_reg[rjj],
		         (unsigned long long)(addr + 4 + imm));
		return emit_result(out, addr, offset, 4, bytes, size, "bnez", ops);
	}

	/* ---- lu12i.w (bits 31:24 = 0x14, no overlap with B26 b) ---- */
	if (op8(inst) == 0x14 && !(inst & 0x00C00000)) {
		snprintf(ops, sizeof ops, "%s, 0x%x", la_reg[rdd], imm20(inst));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "lu12i.w", ops);
	}

	/* ---- lu32i.d (bits 31:24 = 0x16, bit 23 = 0) ---- */
	if (op8(inst) == 0x16 && !(inst & 0x00800000)) {
		snprintf(ops, sizeof ops, "%s, 0x%x", la_reg[rdd], imm20(inst));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "lu32i.d", ops);
	}

	/* ---- lu52i.d (bits 31:24 = 0x16, bit 23 = 1) ---- */
	if (op8(inst) == 0x16 && (inst & 0x00800000)) {
		int64_t v = sext_u(imm12(inst), 12);
		if (v < 0)
			snprintf(ops, sizeof ops, "%s, %s, -0x%llx",
			         la_reg[rdd], la_reg[rjj],
			         (unsigned long long)(-v));
		else
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         la_reg[rdd], la_reg[rjj],
			         (unsigned long long)v);
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "lu52i.d", ops);
	}

	/* ---- pcalau12i (bits 31:24 = 0x1A) ---- */
	if (op8(inst) == 0x1A) {
		snprintf(ops, sizeof ops, "%s, 0x%x", la_reg[rdd], imm20(inst));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "pcalau12i", ops);
	}

	/* ---- Load / Store I-type (bits 31:22 = 0x0A0-0x0AA) ---- */
	if (op10(inst) >= 0x0A0 && op10(inst) <= 0x0AA) {
		static const char *const ldst[11] = {
			"ld.b", "ld.h", "ld.w", "ld.d",
			"st.b", "st.h", "st.w", "st.d",
			"ld.bu", "ld.hu", "ld.wu"
		};
		int idx = op10(inst) - 0x0A0;

		imm = sext_u(imm12(inst), 12);
		if (idx >= 4 && idx <= 7)
			snprintf(rs, sizeof rs, "%s", la_reg[rdd]);
		else
			snprintf(rs, sizeof rs, "%s", la_reg[rdd]);
		snprintf(rt, sizeof rt, "%s", la_reg[rjj]);
		if (imm == 0)
			snprintf(ops, sizeof ops, "%s, 0(%s)", rs, rt);
		else if (imm > 0)
			snprintf(ops, sizeof ops, "%s, 0x%llx(%s)", rs,
			         (unsigned long long)imm, rt);
		else
			snprintf(ops, sizeof ops, "%s, -0x%llx(%s)", rs,
			         (unsigned long long)(-imm), rt);
		return emit_result(out, addr, offset, 4, bytes, size,
		                   ldst[idx], ops);
	}

	/* ---- syscall (bits 31:24 = 0x18) ---- */
	if ((inst & 0xFF000000) == 0x18000000) {
		unsigned code = inst & 0x1F;
		if (code)
			snprintf(ops, sizeof ops, "0x%x", code);
		else
			ops[0] = '\0';
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "syscall", ops);
	}

	/* ---- break (bits 31:24 = 0xB0) ---- */
	if ((inst & 0xFF000000) == 0xB0000000) {
		unsigned code = (inst >> 10) & 0x7FFF;
		if (code)
			snprintf(ops, sizeof ops, "0x%x", code);
		else
			ops[0] = '\0';
		return emit_result(out, addr, offset, 4, bytes, size,
		                   "break", ops);
	}

	/* ---- dbar / ibcl (bits 31:28 = 0x0B) ---- */
	if (op8(inst) == 0x0B) {
		unsigned sub = (inst >> 5) & 0x1F;
		if (sub == 0) {
			unsigned hint = inst & 0x1F;
			if (hint)
				snprintf(ops, sizeof ops, "0x%x", hint);
			else
				ops[0] = '\0';
			return emit_result(out, addr, offset, 4, bytes, size,
			                   "dbar", ops);
		}
		if (sub == 4) {
			ops[0] = '\0';
			return emit_result(out, addr, offset, 4, bytes, size,
			                   "ibcl", ops);
		}
	}

	/* ---- I-type ALU (bits 31:22 = 10-bit opcode) ---- */
	if (op10(inst) == 0x008 || op10(inst) == 0x009 ||
	    op10(inst) == 0x00A || op10(inst) == 0x00B ||
	    op10(inst) == 0x00D || op10(inst) == 0x00E ||
	    op10(inst) == 0x00F) {
		static const char *const alu_imm[] = {
			[0x008] = "slti",
			[0x009] = "sltui",
			[0x00A] = "addi.w",
			[0x00B] = "addi.d",
			[0x00D] = "andi",
			[0x00E] = "ori",
			[0x00F] = "xori",
		};
		imm = sext_u(imm12(inst), 12);
		if (op10(inst) >= 0x00D && op10(inst) <= 0x00F)
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         la_reg[rdd], la_reg[rjj],
			         (unsigned long long)(imm12(inst)));
		else if (imm >= 0)
			snprintf(ops, sizeof ops, "%s, %s, 0x%llx",
			         la_reg[rdd], la_reg[rjj],
			         (unsigned long long)imm);
		else
			snprintf(ops, sizeof ops, "%s, %s, -0x%llx",
			         la_reg[rdd], la_reg[rjj],
			         (unsigned long long)(-imm));
		return emit_result(out, addr, offset, 4, bytes, size,
		                   alu_imm[op10(inst)], ops);
	}

	/* ---- R-type (3-register) and shift-type (2RI14) ---- */
	switch (func17(inst)) {
	case 0x20:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "add.w", ops);
	case 0x21:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "add.d", ops);
	case 0x22:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sub.w", ops);
	case 0x23:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sub.d", ops);
	case 0x24:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "slt", ops);
	case 0x25:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sltu", ops);
	case 0x28:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "nor", ops);
	case 0x29:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "and", ops);
	case 0x2A:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "or", ops);
	case 0x2B:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "xor", ops);
	case 0x2E:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sll.w", ops);
	case 0x2F:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "srl.w", ops);
	case 0x30:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sra.w", ops);
	case 0x31:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sll.d", ops);
	case 0x32:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "srl.d", ops);
	case 0x33:
		snprintf(ops, sizeof ops, "%s, %s, %s",
		         la_reg[rdd], la_reg[rjj], la_reg[rkk]);
		return emit_result(out, addr, offset, 4, bytes, size, "sra.d", ops);
	case 0x81:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt5(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "slli.w", ops);
	case 0x82:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt6(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "slli.d", ops);
	case 0x89:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt5(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "srli.w", ops);
	case 0x8A:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt6(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "srli.d", ops);
	case 0x91:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt5(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "srai.w", ops);
	case 0x92:
		snprintf(ops, sizeof ops, "%s, %s, 0x%x",
		         la_reg[rdd], la_reg[rjj], shamt6(inst));
		return emit_result(out, addr, offset, 4, bytes, size, "srai.d", ops);
	default:
		return fail(out, addr, offset, 4, bytes, size);
	}
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

int
mt_disasm_loongarch64_one(const unsigned char *bytes, size_t size,
                          size_t offset, uint64_t addr,
                          struct mt_disasm_insn *out)
{
	if (offset >= size) {
		out->address = addr;
		out->offset = offset;
		out->length = 1;
		out->operands[0] = '\0';
		out->bytes_hex[0] = '\0';
		snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
		return -1;
	}

	return decode_one(bytes, size, offset, addr, out);
}
