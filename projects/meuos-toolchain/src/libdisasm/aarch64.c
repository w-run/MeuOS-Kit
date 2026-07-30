/* aarch64.c - AArch64 (ARMv8) disassembler.
 *
 * Implements mt_disasm_aarch64_one(): decode a single AArch64 instruction
 * from a raw byte stream and produce ARM syntax mnemonic + operands.
 *
 * All AArch64 instructions are 32 bits, fixed-length, little-endian.
 *
 * Coverage: P0 integer/control-flow/system (mcc/QBE/libc core).
 */
#include "mt/disasm.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Register name tables                                                */
/* ------------------------------------------------------------------ */

static const char *const xreg[32] = {
	"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
	"x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	"x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
	"x24", "x25", "x26", "x27", "x28", "x29", "x30", "xzr"
};

static const char *const wreg[32] = {
	"w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7",
	"w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15",
	"w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
	"w24", "w25", "w26", "w27", "w28", "w29", "w30", "wzr"
};

/* Condition suffixes for B.cond. */
static const char *const cond_name[16] = {
	"eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
	"hi", "ls", "ge", "lt", "gt", "le", "al", "nv"
};

/* Extend types. */
static const char *const ext_name[4] = {
	"uxtb", "uxth", "uxtw", "uxtx"
};
static const char *const sext_name[4] = {
	"sxtb", "sxth", "sxtw", "sxtx"
};

/* Shift types for register shifts. */
static const char *const sh_name[4] = {
	"lsl", "lsr", "asr", "ror"
};

/* ------------------------------------------------------------------ */
/* Helper: return 64-bit or 32-bit register name by sf flag.          */
/* ------------------------------------------------------------------ */

static const char *
rn(int reg, int sf)
{
	return sf ? xreg[reg & 31] : wreg[reg & 31];
}

/* Like rn but r31 becomes sp/wsp. */
static const char *
rn_sp(int reg, int sf)
{
	int r = reg & 31;
	if (r == 31)
		return sf ? "sp" : "wsp";
	return sf ? xreg[r] : wreg[r];
}

/* ------------------------------------------------------------------ */
/* Sign-extend 'bits'-bit value.                                       */
/* ------------------------------------------------------------------ */

static int64_t
sext(uint64_t val, int bits)
{
	uint64_t m = 1ULL << (bits - 1);
	uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
	val &= mask;
	return (int64_t)((val ^ m) - m);
}

/* ------------------------------------------------------------------ */
/* Extract bitfield [hi:lo] from instruction.                          */
/* ------------------------------------------------------------------ */

static inline uint32_t
bf(uint32_t insn, int hi, int lo)
{
	int w = hi - lo + 1;
	return (insn >> lo) & ((1u << w) - 1);
}

/* ------------------------------------------------------------------ */
/* String builder for operand formatting.                              */
/* ------------------------------------------------------------------ */

struct sb {
	char *p;
	size_t cap;
	size_t len;
};

static void
sb_init(struct sb *s, char *buf, size_t cap)
{
	s->p = buf;
	s->cap = cap;
	s->len = 0;
	buf[0] = '\0';
}

static void
sb_c(struct sb *s, char c)
{
	if (s->len + 1 < s->cap)
		s->p[s->len++] = c;
	s->p[s->len] = '\0';
}

static void
sb_s(struct sb *s, const char *str)
{
	while (*str)
		sb_c(s, *str++);
}

static void
sb_f(struct sb *s, const char *fmt, ...)
{
	char tmp[96];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	sb_s(s, tmp);
}

/* ------------------------------------------------------------------ */
/* Hex formatting for output (4 bytes = "xx xx xx xx").                */
/* ------------------------------------------------------------------ */

static void
fill_hex(const unsigned char *bytes, size_t size, size_t start,
         struct mt_disasm_insn *out)
{
	char *p = out->bytes_hex;
	size_t cap = sizeof out->bytes_hex;
	const size_t n = 4;
	size_t i;
	static const char hex[] = "0123456789abcdef";

	p[0] = '\0';
	if (start + n > size)
		return;
	for (i = 0; i < n; ++i) {
		unsigned byte = bytes[start + i];
		if (i) {
			if ((size_t)(p - out->bytes_hex) + 1 < cap)
				*p++ = ' ';
		}
		if ((size_t)(p - out->bytes_hex) + 2 < cap) {
			*p++ = hex[(byte >> 4) & 15];
			*p++ = hex[byte & 15];
		}
	}
	*p = '\0';
}

/* ------------------------------------------------------------------ */
/* Finish: fill output structure after successful decode.               */
/* ------------------------------------------------------------------ */

static void
finish(struct mt_disasm_insn *out, const unsigned char *bytes,
       size_t size, size_t offset, uint64_t addr,
       const char *mnem, const char *ops)
{
	out->address = addr;
	out->offset = offset;
	out->length = 4;
	snprintf(out->mnemonic, sizeof out->mnemonic, "%s", mnem);
	snprintf(out->operands, sizeof out->operands, "%s", ops);
	fill_hex(bytes, size, offset, out);
}

/* ------------------------------------------------------------------ */
/* Decode a load/store unsigned immediate (LDR/STR immediate).         */
/* Returns 0 on match, -1 otherwise.                                   */
/* ------------------------------------------------------------------ */

static int
decode_ldst_uimm(uint32_t insn, char *mnem, size_t mnem_cap,
                 char *ops, size_t ops_cap,
                 const unsigned char *bytes, size_t size, size_t offset,
                 uint64_t addr, struct mt_disasm_insn *out)
{
	int size_f = bf(insn, 31, 30);
	int V = bf(insn, 26, 26);
	int opc0 = bf(insn, 22, 22); /* bit[22]: 0=store, 1=load */
	int rn_reg = bf(insn, 9, 5);
	int rt = bf(insn, 4, 0);
	int imm12 = bf(insn, 21, 10);
	int scale[] = {1, 2, 4, 8};
	int s = scale[size_f & 3];
	int64_t imm = (int64_t)imm12 * s;
	const char *base_name = opc0 ? "ldr" : "str";
	const char *suffix = (size_f == 0) ? "b" : (size_f == 1) ? "h" : "";
	const char *rt_name;

	(void)V;
	(void)mnem_cap;
	(void)ops_cap;

	/* Check bits[29:24] = 111001 (V=0), bits[23:22] opc in {00,01,10,11} */
	/* bits[29:24] = 111001 where V is bit[26] of encoding. */
	uint32_t op29_24 = bf(insn, 29, 24);
	if (V != 0)
		return -1;
	if ((op29_24 & 0x3B) != 0x38 || (op29_24 & 0x04) != 0)
		return -1;

	/* Check: this is unsigned immediate encoding when bit[24]=0 and bits[11:10]=00 */
	if (bf(insn, 24, 24) != 0)
		return -1;
	if (bf(insn, 11, 10) != 0)
		return -1;
	/* bit[21] must be 1 for unsigned offset immediate encoding */
	if (bf(insn, 21, 21) != 1)
		return -1;

	if (size_f == 2 || size_f == 3)
		rt_name = (size_f == 3) ? xreg[rt] : wreg[rt];
	else
		rt_name = wreg[rt];

	snprintf(mnem, mnem_cap, "%s%s", base_name, suffix);

	{
		struct sb sb;
		sb_init(&sb, ops, ops_cap);
		sb_s(&sb, rt_name);
		sb_s(&sb, ", [");
		sb_s(&sb, rn_sp(rn_reg, 1));
		if (imm != 0)
			sb_f(&sb, ", #%" PRId64, imm);
		sb_s(&sb, "]");
	}
	finish(out, bytes, size, offset, addr, mnem, ops);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode load/store register offset (LDR/STR register).               */
/* Returns 0 on match, -1 otherwise.                                   */
/* ------------------------------------------------------------------ */

static int
decode_ldst_reg(uint32_t insn, char *mnem, size_t mnem_cap,
                char *ops, size_t ops_cap,
                const unsigned char *bytes, size_t size, size_t offset,
                uint64_t addr, struct mt_disasm_insn *out)
{
	int size_f = bf(insn, 31, 30);
	int V = bf(insn, 26, 26);
	int rn_reg = bf(insn, 9, 5);
	int rt = bf(insn, 4, 0);
	int opc0 = bf(insn, 22, 22);
	int rm = bf(insn, 20, 16);
	int option = bf(insn, 15, 13);
	int S = bf(insn, 12, 12);
	const char *base_name = opc0 ? "ldr" : "str";
	const char *suffix = (size_f == 0) ? "b" : (size_f == 1) ? "h" : "";
	const char *rt_name;
	struct sb sb;

	(void)S;

	if (V != 0)
		return -1;
	/* Check bits[29:24] = 111001 (V=0) */
	uint32_t op29_24 = bf(insn, 29, 24);
	if ((op29_24 & 0x3B) != 0x38 || (op29_24 & 0x04) != 0)
		return -1;
	if (bf(insn, 24, 24) != 0)
		return -1;
	/* Register offset: bit[21]=0, bit[12]=1, bits[11:10]=00 */
	if (bf(insn, 21, 21) != 0)
		return -1;
	if (bf(insn, 12, 12) != 1)
		return -1;
	if (bf(insn, 11, 10) != 0)
		return -1;

	if (size_f == 2 || size_f == 3)
		rt_name = (size_f == 3) ? xreg[rt] : wreg[rt];
	else
		rt_name = wreg[rt];

	snprintf(mnem, mnem_cap, "%s%s", base_name, suffix);
	sb_init(&sb, ops, ops_cap);
	sb_s(&sb, rt_name);
	sb_s(&sb, ", [");
	sb_s(&sb, rn_sp(rn_reg, 1));
	sb_s(&sb, ", ");
	sb_s(&sb, (size_f >= 2) ? xreg[rm] : wreg[rm]);
	if (option != 0 || S != 0) {
		sb_s(&sb, ", ");
		sb_s(&sb, ext_name[option & 3]);
		if (S)
			sb_f(&sb, " #%d", size_f == 3 ? 3 : size_f == 2 ? 2 : 0);
	}
	sb_s(&sb, "]");
	finish(out, bytes, size, offset, addr, mnem, ops);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode load/store unscaled offset (LDUR/STUR).                      */
/* Returns 0 on match, -1 otherwise.                                   */
/* ------------------------------------------------------------------ */

static int
decode_ldst_unscaled(uint32_t insn, char *mnem, size_t mnem_cap,
                     char *ops, size_t ops_cap,
                     const unsigned char *bytes, size_t size, size_t offset,
                     uint64_t addr, struct mt_disasm_insn *out)
{
	int size_f = bf(insn, 31, 30);
	int V = bf(insn, 26, 26);
	int rn_reg = bf(insn, 9, 5);
	int rt = bf(insn, 4, 0);
	int64_t imm9 = sext(bf(insn, 20, 12), 9);
	const char *name;
	const char *rt_name;
	struct sb sb;

	if (V != 0)
		return -1;

	/* Check bits[29:24] = 111001 but with bit[21]=0 (unscaled), bits[11:10]=00. */
	/* Actually unscaled offset has bits[29:24] = 111001 (same base) but
	 * with opc field bits[23:22] indicating load/store */
	uint32_t op29_24 = bf(insn, 29, 24);
	if ((op29_24 & 0x3B) != 0x38 || (op29_24 & 0x04) != 0)
		return -1;
	if (bf(insn, 24, 24) != 0)
		return -1;
	/* Unscaled offset: bit[21]=0, bits[11:10]=00 */
	if (bf(insn, 21, 21) != 0)
		return -1;
	if (bf(insn, 11, 10) != 0)
		return -1;
	/* Register offset has bit[12]=1, unscaled has bit[12]=0 */
	if (bf(insn, 12, 12) != 0)
		return -1;

	int opc0 = bf(insn, 22, 22);

	static const char *const stur_names[4] = {"sturb", "sturh", "stur", "stur"};
	static const char *const ldur_names[4] = {"ldurb", "ldurh", "ldur", "ldur"};
	name = opc0 ? ldur_names[size_f] : stur_names[size_f];

	if (size_f == 2 || size_f == 3)
		rt_name = (size_f == 3) ? xreg[rt] : wreg[rt];
	else
		rt_name = wreg[rt];

	snprintf(mnem, mnem_cap, "%s", name);
	sb_init(&sb, ops, ops_cap);
	sb_s(&sb, rt_name);
	sb_s(&sb, ", [");
	sb_s(&sb, rn_sp(rn_reg, 1));
	sb_f(&sb, ", #%" PRId64, imm9);
	sb_s(&sb, "]");
	finish(out, bytes, size, offset, addr, mnem, ops);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode load/store pair (STP/LDP).                                    */
/* Returns 0 on match, -1 otherwise.                                   */
/* ------------------------------------------------------------------ */

static int
decode_ldp_stp(uint32_t insn, char *mnem, size_t mnem_cap,
               char *ops, size_t ops_cap,
               const unsigned char *bytes, size_t size, size_t offset,
               uint64_t addr, struct mt_disasm_insn *out)
{
	int size_f = bf(insn, 31, 30);
	int L = bf(insn, 22, 22);
	int rt = bf(insn, 4, 0);
	int rn_reg = bf(insn, 9, 5);
	int rt2 = bf(insn, 14, 10);
	int64_t imm7;
	const char *name = L ? "ldp" : "stp";
	struct sb sb;

	(void)mnem_cap;

	/* Check bits[29:24] = 100101 for STP/LDP, bit[23]=1, bit[24]=V=0 */
	uint32_t op29_24 = bf(insn, 29, 24);
	if ((op29_24 & 0x3F) != 0x25)
		return -1;
	if (bf(insn, 24, 24) != 0)
		return -1;
	if (bf(insn, 23, 23) != 1)
		return -1;
	/* bits[15:14] = 10 for scaled signed offset variant */
	if (bf(insn, 15, 14) != 2)
		return -1;

	if (size_f != 1 && size_f != 2)
		return -1;

	int scale = (size_f == 2) ? 8 : 4;
	imm7 = sext(bf(insn, 21, 15), 7) * scale;

	snprintf(mnem, mnem_cap, "%s", name);
	sb_init(&sb, ops, ops_cap);
	sb_s(&sb, xreg[rt]);
	sb_s(&sb, ", ");
	sb_s(&sb, xreg[rt2]);
	sb_s(&sb, ", [");
	sb_s(&sb, rn_sp(rn_reg, 1));
	if (imm7 != 0)
		sb_f(&sb, ", #%" PRId64, imm7);
	sb_s(&sb, "]");
	finish(out, bytes, size, offset, addr, mnem, ops);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Decode LDR literal.                                                  */
/* Returns 0 on match, -1 otherwise.                                   */
/* ------------------------------------------------------------------ */

static int
decode_ldr_literal(uint32_t insn, char *mnem, size_t mnem_cap,
                   char *ops, size_t ops_cap,
                   const unsigned char *bytes, size_t size, size_t offset,
                   uint64_t addr, struct mt_disasm_insn *out)
{
	int opc = bf(insn, 31, 30);
	int rt = bf(insn, 4, 0);
	int64_t off = sext(bf(insn, 23, 5), 19) * 4;
	const char *name;
	const char *rt_name;
	struct sb sb;

	/* Check bits[29:24] = 011000 (with bit[29]=0) */
	uint32_t op28_24 = bf(insn, 28, 24);
	if (bf(insn, 29, 29) != 0)
		return -1;
	if (op28_24 != 0x18)
		return -1;

	switch (opc) {
	case 0: name = "ldrsw"; rt_name = xreg[rt]; break;
	case 1: name = "ldr";   rt_name = wreg[rt]; break;
	case 2: name = "ldr";   rt_name = xreg[rt]; break;
	default: return -1;
	}

	snprintf(mnem, mnem_cap, "%s", name);
	sb_init(&sb, ops, ops_cap);
	sb_s(&sb, rt_name);
	sb_s(&sb, ", ");
	sb_f(&sb, "%#" PRIx64, addr + off);
	finish(out, bytes, size, offset, addr, mnem, ops);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Public entry point.                                                 */
/* ------------------------------------------------------------------ */

int
mt_disasm_aarch64_one(const unsigned char *bytes, size_t size,
                      size_t offset, uint64_t addr,
                      struct mt_disasm_insn *out)
{
	uint32_t insn;
	char mnem[MT_DISASM_MNEM_CAP];
	char ops[MT_DISASM_OPS_CAP];
	struct sb sb;

	/* Must have at least 4 bytes. */
	if (offset + 4 > size) {
	bad:
		out->address = addr;
		out->offset = offset;
		out->length = 1;
		out->operands[0] = '\0';
		out->bytes_hex[0] = '\0';
		snprintf(out->mnemonic, sizeof out->mnemonic, "(bad)");
		return -1;
	}

	insn = (uint32_t)bytes[offset]
	     | ((uint32_t)bytes[offset + 1] << 8)
	     | ((uint32_t)bytes[offset + 2] << 16)
	     | ((uint32_t)bytes[offset + 3] << 24);

	/* ============================================================== */
	/* NOP                                                             */
	/* ============================================================== */
	if (insn == 0xD503201F) {
		finish(out, bytes, size, offset, addr, "nop", "");
		return 0;
	}

	/* ============================================================== */
	/* SVC (supervisor call)                                           */
	/* ============================================================== */
	if ((insn & 0xFF00001F) == 0xD4000001) {
		uint16_t imm16 = (uint16_t)bf(insn, 20, 5);
		sb_init(&sb, ops, sizeof ops);
		sb_f(&sb, "#%u", (unsigned)imm16);
		finish(out, bytes, size, offset, addr, "svc", ops);
		return 0;
	}

	/* ============================================================== */
	/* B (unconditional immediate): bits[31:26] = 000101                */
	/* ============================================================== */
	if (bf(insn, 31, 26) == 0x05) {
		int64_t off = sext(bf(insn, 25, 0), 26) * 4;
		sb_init(&sb, ops, sizeof ops);
		sb_f(&sb, "%#" PRIx64, (uint64_t)(addr + off));
		finish(out, bytes, size, offset, addr, "b", ops);
		return 0;
	}

	/* ============================================================== */
	/* BL (branch-and-link immediate): bits[31:26] = 100101             */
	/* ============================================================== */
	if (bf(insn, 31, 26) == 0x25) {
		int64_t off = sext(bf(insn, 25, 0), 26) * 4;
		sb_init(&sb, ops, sizeof ops);
		sb_f(&sb, "%#" PRIx64, (uint64_t)(addr + off));
		finish(out, bytes, size, offset, addr, "bl", ops);
		return 0;
	}

	/* ============================================================== */
	/* B.cond: bits[31:25] = 0101010, bit[4] = 0                       */
	/* ============================================================== */
	if (bf(insn, 31, 25) == 0x2A && (insn & 0x10) == 0) {
		unsigned cond = bf(insn, 3, 0);
		int64_t off = sext(bf(insn, 23, 5), 19) * 4;
		sb_init(&sb, ops, sizeof ops);
		sb_f(&sb, "%#" PRIx64, (uint64_t)(addr + off));
		snprintf(mnem, sizeof mnem, "b.%s", cond_name[cond & 15]);
		finish(out, bytes, size, offset, addr, mnem, ops);
		return 0;
	}

	/* ============================================================== */
	/* CBZ / CBNZ: bits[30:25] = 011010                                 */
	/*   sf = bits[31], op = bits[24] (0=CBZ, 1=CBNZ)                   */
	/* ============================================================== */
	if (bf(insn, 30, 25) == 0x1A) {
		int sf = bf(insn, 31, 31);
		int op = bf(insn, 24, 24);
		int rt_r = bf(insn, 4, 0);
		int64_t off = sext(bf(insn, 23, 5), 19) * 4;
		const char *name = op ? "cbnz" : "cbz";
		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rt_r, sf));
		sb_s(&sb, ", ");
		sb_f(&sb, "%#" PRIx64, (uint64_t)(addr + off));
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* TBZ / TBNZ: bits[30:25] = 011011                                 */
	/*   bit[31] = b5, bit[24] = op (0=TBZ,1=TBNZ)                      */
	/*   bits[23:19] = b40, bits[18:5] = imm14, bits[4:0] = Rt          */
	/* ============================================================== */
	if (bf(insn, 30, 25) == 0x1B) {
		int b5 = bf(insn, 31, 31);
		int op = bf(insn, 24, 24);
		int b40 = bf(insn, 23, 19);
		int bit_pos = (b5 << 5) | b40;
		int rt_r = bf(insn, 4, 0);
		int64_t off = sext(bf(insn, 18, 5), 14) * 4;
		const char *name = op ? "tbnz" : "tbz";
		sb_init(&sb, ops, sizeof ops);
		sb_f(&sb, "#%d, ", bit_pos);
		sb_s(&sb, xreg[rt_r]);
		sb_s(&sb, ", ");
		sb_f(&sb, "%#" PRIx64, (uint64_t)(addr + off));
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* Unconditional branch (register): BR / BLR / RET                  */
	/*   bits[31:24] = 11010110, bits[20:16] = 11111,                  */
	/*   bits[15:10] = 000000, bits[4:0] = 00000                       */
	/*   opc = bits[22:21] -> 00=BR, 01=BLR, 10=RET                    */
	/* ============================================================== */
	if ((insn & 0xFFE00000) == 0xD6000000 &&
	    bf(insn, 20, 16) == 0x1F &&
	    (insn & 0x0000FC1F) == 0) {
		int opc = bf(insn, 22, 21);
		int rn_r = bf(insn, 9, 5);
		static const char *const br_name[4] = {
			"br", "blr", "ret", NULL
		};
		if (opc <= 2 && br_name[opc]) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, xreg[rn_r]);
			finish(out, bytes, size, offset, addr, br_name[opc], ops);
			return 0;
		}
	}

	/* ============================================================== */
	/* Load/store (immediate unsigned offset, register offset,          */
	/* unscaled offset, load literal, load/store pair)                  */
	/* ============================================================== */

	/* Try load/store pair (STP/LDP) first. */
	if (decode_ldp_stp(insn, mnem, sizeof mnem, ops, sizeof ops,
	                   bytes, size, offset, addr, out) == 0)
		return 0;

	/* Try LDR literal (has distinct bit pattern). */
	if (decode_ldr_literal(insn, mnem, sizeof mnem, ops, sizeof ops,
	                       bytes, size, offset, addr, out) == 0)
		return 0;

	/* Try unscaled offset (LDUR/STUR). Must check before unsigned imm
	 * since they share similar bit patterns. Unscaled has bit[21]=0
	 * while unsigned immediate has bit[21]=1 in the load/store group. */
	if (decode_ldst_unscaled(insn, mnem, sizeof mnem, ops, sizeof ops,
	                         bytes, size, offset, addr, out) == 0)
		return 0;

	/* Try unsigned immediate offset (LDR/STR with imm12). */
	if (decode_ldst_uimm(insn, mnem, sizeof mnem, ops, sizeof ops,
	                     bytes, size, offset, addr, out) == 0)
		return 0;

	/* Try register offset. */
	if (decode_ldst_reg(insn, mnem, sizeof mnem, ops, sizeof ops,
	                    bytes, size, offset, addr, out) == 0)
		return 0;

	/* ============================================================== */
	/* ADD/SUB immediate:                                              */
	/*   bits[28:24] = 10001, bit[23] = 0                              */
	/*   sf[31], op[30], S[29], shift[23:22], imm12[21:10],            */
	/*   Rn[9:5], Rd[4:0]                                              */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x11 && bf(insn, 23, 23) == 0) {
		int sf = bf(insn, 31, 31);
		int op = bf(insn, 30, 30);
		int S = bf(insn, 29, 29);
		int shift = bf(insn, 23, 22);
		int imm12 = bf(insn, 21, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		const char *name;
		int64_t imm;

		if (op == 0 && S == 0) name = "add";
		else if (op == 1 && S == 0) name = "sub";
		else if (op == 0 && S == 1) name = "adds";
		else name = "subs";

		imm = imm12;
		if (shift == 1) imm <<= 12;

		/* CMP: SUDS XZR, Rn, #imm */
		if (rd == 31 && op == 1 && S == 1) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rn_r, sf));
			sb_s(&sb, ", ");
			sb_f(&sb, "#%" PRId64, imm);
			finish(out, bytes, size, offset, addr, "cmp", ops);
			return 0;
		}

		/* MOV: ADD Rd, XZR, #imm (or ADD Wd, WZR, #imm) */
		if (rn_r == 31 && op == 0 && S == 0 && imm != 0) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_f(&sb, "#%" PRId64, imm);
			finish(out, bytes, size, offset, addr, "mov", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_f(&sb, "#%" PRId64, imm);
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* ADD/SUB (extended register):                                     */
	/*   bits[28:24] = 01011, bits[23:22] = 00, bit[21] = 1            */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x0B && bf(insn, 23, 22) == 0 && bf(insn, 21, 21) == 1) {
		int sf = bf(insn, 31, 31);
		int op = bf(insn, 30, 30);
		int S = bf(insn, 29, 29);
		int rm_r = bf(insn, 20, 16);
		int option = bf(insn, 15, 13);
		int imm3 = bf(insn, 12, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		const char *name;

		if (S)
			name = op ? "subs" : "adds";
		else
			name = op ? "sub" : "add";

		/* CMP: SUBS XZR, ... */
		if (rd == 31 && op && S) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rn_r, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			if (option != 0 || imm3 != 0) {
				sb_s(&sb, ", ");
				sb_s(&sb, option < 4 ? ext_name[option] : sext_name[option & 3]);
				if (imm3 != 0)
					sb_f(&sb, " #%d", imm3);
			}
			finish(out, bytes, size, offset, addr, "cmp", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rm_r, sf));
		if (option != 0 || imm3 != 0) {
			sb_s(&sb, ", ");
			sb_s(&sb, option < 4 ? ext_name[option] : sext_name[option & 3]);
			if (imm3 != 0)
				sb_f(&sb, " #%d", imm3);
		}
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* ADD/SUB (shifted register): bits[28:24] = 01011, bit[21] = 0    */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x0B && bf(insn, 21, 21) == 0) {
		int sf = bf(insn, 31, 31);
		int op = bf(insn, 30, 30);
		int S = bf(insn, 29, 29);
		int shift = bf(insn, 23, 22);
		int rm_r = bf(insn, 20, 16);
		int imm6 = bf(insn, 15, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		const char *name;

		if (S)
			name = op ? "subs" : "adds";
		else
			name = op ? "sub" : "add";

		/* MOV: ADD Rd, XZR, Rm (shift=0, imm6=0) */
		if (rn_r == 31 && shift == 0 && imm6 == 0 && !op && !S) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			finish(out, bytes, size, offset, addr, "mov", ops);
			return 0;
		}

		/* CMP: SUBS XZR, Rn, Rm */
		if (rd == 31 && op && S) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rn_r, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			if (shift != 0 || imm6 != 0) {
				sb_s(&sb, ", ");
				sb_s(&sb, sh_name[shift]);
				sb_f(&sb, " #%d", imm6);
			}
			finish(out, bytes, size, offset, addr, "cmp", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rm_r, sf));
		if (shift != 0 || imm6 != 0) {
			sb_s(&sb, ", ");
			sb_s(&sb, sh_name[shift]);
			sb_f(&sb, " #%d", imm6);
		}
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* Logical immediate: AND/ORR/EOR/ANDS                              */
	/*   bits[28:23] = 100100                                         */
	/*   sf[31], opc[30:29], N[22], immr[21:16], imms[15:10],          */
	/*   Rn[9:5], Rd[4:0]                                              */
	/* ============================================================== */
	if (bf(insn, 28, 23) == 0x24) {
		int sf = bf(insn, 31, 31);
		int opc = bf(insn, 30, 29);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		static const char *const logic_names[4] = {
			"and", "orr", "eor", "ands"
		};
		const char *name = logic_names[opc];

		/* MOV alias: ORR Rd, XZR, #imm */
		if (opc == 1 && rn_r == 31) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_f(&sb, "#0x%x", bf(insn, 21, 10));
			finish(out, bytes, size, offset, addr, "mov", ops);
			return 0;
		}

		/* TST alias: ANDS XZR, Rn, #imm */
		if (opc == 3 && rd == 31) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rn_r, sf));
			sb_s(&sb, ", ");
			sb_f(&sb, "#0x%x", bf(insn, 21, 10));
			finish(out, bytes, size, offset, addr, "tst", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_f(&sb, "#0x%x", bf(insn, 21, 10));
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* Logical shifted register: AND/ORR/EOR/ANDS (register)           */
	/*   bits[28:24] = 01010, bit[21]=0                                */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x0A && bf(insn, 21, 21) == 0) {
		int sf = bf(insn, 31, 31);
		int opc = bf(insn, 30, 29);
		int shift = bf(insn, 23, 22);
		int rm_r = bf(insn, 20, 16);
		int imm6 = bf(insn, 15, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		static const char *const logic_names[4] = {
			"and", "orr", "eor", "ands"
		};
		const char *name = logic_names[opc];

		/* MOV: ORR Rd, XZR, Rm */
		if (opc == 1 && rn_r == 31 && shift == 0 && imm6 == 0) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			finish(out, bytes, size, offset, addr, "mov", ops);
			return 0;
		}

		/* TST: ANDS XZR, Rn, Rm */
		if (opc == 3 && rd == 31) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rn_r, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			if (shift != 0 || imm6 != 0) {
				sb_s(&sb, ", ");
				sb_s(&sb, sh_name[shift]);
				sb_f(&sb, " #%d", imm6);
			}
			finish(out, bytes, size, offset, addr, "tst", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rm_r, sf));
		if (shift != 0 || imm6 != 0) {
			sb_s(&sb, ", ");
			sb_s(&sb, sh_name[shift]);
			sb_f(&sb, " #%d", imm6);
		}
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* BIC/ORN/EON/BICS (logical shifted with bit[21]=1)               */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x0A && bf(insn, 21, 21) == 1) {
		int sf = bf(insn, 31, 31);
		int opc = bf(insn, 30, 29);
		int shift = bf(insn, 23, 22);
		int rm_r = bf(insn, 20, 16);
		int imm6 = bf(insn, 15, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		static const char *const logic_inv[4] = {
			"bic", "orn", "eon", "bics"
		};
		const char *name = logic_inv[opc];

		/* MVN: ORN Rd, XZR, Rm */
		if (opc == 1 && rn_r == 31) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rm_r, sf));
			if (shift != 0 || imm6 != 0) {
				sb_s(&sb, ", ");
				sb_s(&sb, sh_name[shift]);
				sb_f(&sb, " #%d", imm6);
			}
			finish(out, bytes, size, offset, addr, "mvn", ops);
			return 0;
		}

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rn_r, sf));
		sb_s(&sb, ", ");
		sb_s(&sb, rn(rm_r, sf));
		if (shift != 0 || imm6 != 0) {
			sb_s(&sb, ", ");
			sb_s(&sb, sh_name[shift]);
			sb_f(&sb, " #%d", imm6);
		}
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
	}

	/* ============================================================== */
	/* Move wide immediate: MOVN/MOVZ/MOVK                             */
	/*   bits[28:23] = 100101                                         */
	/* ============================================================== */
	if (bf(insn, 28, 23) == 0x25) {
		int sf = bf(insn, 31, 31);
		int opc = bf(insn, 30, 29);
		int hw = bf(insn, 22, 21);
		int imm16 = bf(insn, 20, 5);
		int rd = bf(insn, 4, 0);
		int shift = hw * 16;
		static const char *const mv_names[4] = {
			"movn", "movz", "movk", NULL
		};
		const char *name;

		if (opc >= 2)
			goto not_movw;
		name = mv_names[opc];

		sb_init(&sb, ops, sizeof ops);
		sb_s(&sb, rn(rd, sf));
		sb_s(&sb, ", ");
		sb_f(&sb, "#0x%x", imm16);
		if (shift != 0)
			sb_f(&sb, ", lsl #%d", shift);
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
		not_movw:;
	}

	/* ============================================================== */
	/* Bitfield move: SBFM/UBFM/BFM (LSL/LSR/ASR/SXTB/SXTH/SXTW)      */
	/*   bits[28:23] = 100110                                         */
	/* ============================================================== */
	if (bf(insn, 28, 23) == 0x26) {
		int sf = bf(insn, 31, 31);
		int opc = bf(insn, 30, 29);
		int N = bf(insn, 22, 22);
		int immr = bf(insn, 21, 16);
		int imms = bf(insn, 15, 10);
		int rn_r = bf(insn, 9, 5);
		int rd = bf(insn, 4, 0);
		int datasize = sf ? 64 : 32;

		(void)N;

		if (opc == 2) {
			/* UBFM -> LSL/LSR/UXTB/UXTH */
			if (imms == datasize - 1) {
				/* LSR: UBFM Rd, Rn, #shift, #63 */
				int shift = immr;
				if (shift != 0) {
					sb_init(&sb, ops, sizeof ops);
					sb_s(&sb, rn(rd, sf));
					sb_s(&sb, ", ");
					sb_s(&sb, rn(rn_r, sf));
					sb_f(&sb, ", #%d", shift);
					finish(out, bytes, size, offset, addr, "lsr", ops);
					return 0;
				}
			}
			if (immr == 0) {
				int width = imms + 1;
				if (sf == 0 && width == 8) {
					sb_init(&sb, ops, sizeof ops);
					sb_s(&sb, rn(rd, sf));
					sb_s(&sb, ", ");
					sb_s(&sb, rn(rn_r, sf));
					finish(out, bytes, size, offset, addr, "uxtb", ops);
					return 0;
				}
				if (sf == 0 && width == 16) {
					sb_init(&sb, ops, sizeof ops);
					sb_s(&sb, rn(rd, sf));
					sb_s(&sb, ", ");
					sb_s(&sb, rn(rn_r, sf));
					finish(out, bytes, size, offset, addr, "uxth", ops);
					return 0;
				}
			}
			if (immr > 0 && (immr + imms + 1) == datasize) {
				/* LSL: UBFM Rd, Rn, #(-shift mod N), #(N-1-shift) */
				int shift = datasize - immr;
				if (shift > 0 && imms == datasize - 1 - shift) {
					sb_init(&sb, ops, sizeof ops);
					sb_s(&sb, rn(rd, sf));
					sb_s(&sb, ", ");
					sb_s(&sb, rn(rn_r, sf));
					sb_f(&sb, ", #%d", shift);
					finish(out, bytes, size, offset, addr, "lsl", ops);
					return 0;
				}
			}
			/* Generic UBFM */
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rn_r, sf));
			sb_f(&sb, ", #%d, #%d", immr, imms);
			finish(out, bytes, size, offset, addr, "ubfm", ops);
			return 0;
		}

		if (opc == 0) {
			/* SBFM -> ASR/SXTB/SXTH/SXTW */
			if (imms == datasize - 1) {
				int shift = immr;
				sb_init(&sb, ops, sizeof ops);
				sb_s(&sb, rn(rd, sf));
				sb_s(&sb, ", ");
				sb_s(&sb, rn(rn_r, sf));
				sb_f(&sb, ", #%d", shift);
				finish(out, bytes, size, offset, addr, "asr", ops);
				return 0;
			}
			if (immr == 0) {
				int width = imms + 1;
				const char *name;
				if (sf == 0 && width == 8) name = "sxtb";
				else if (sf == 0 && width == 16) name = "sxth";
				else if (sf && width == 32) name = "sxtw";
				else name = NULL;
				if (name) {
					sb_init(&sb, ops, sizeof ops);
					sb_s(&sb, rn(rd, sf));
					sb_s(&sb, ", ");
					sb_s(&sb, rn(rn_r, sf));
					finish(out, bytes, size, offset, addr, name, ops);
					return 0;
				}
			}
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rn_r, sf));
			sb_f(&sb, ", #%d, #%d", immr, imms);
			finish(out, bytes, size, offset, addr, "sbfm", ops);
			return 0;
		}

		if (opc == 1) {
			sb_init(&sb, ops, sizeof ops);
			sb_s(&sb, rn(rd, sf));
			sb_s(&sb, ", ");
			sb_s(&sb, rn(rn_r, sf));
			sb_f(&sb, ", #%d, #%d", immr, imms);
			finish(out, bytes, size, offset, addr, "bfm", ops);
			return 0;
		}
	}

	/* ============================================================== */
	/* Load/store exclusive (LDXR/STXR): bits[29:24] = 001000          */
	/* ============================================================== */
	if (bf(insn, 28, 24) == 0x08 && bf(insn, 23, 22) == 0 && bf(insn, 16, 16) == 0) {
		int size_f = bf(insn, 31, 30);
		int o0 = bf(insn, 15, 15);
		int rn_r = bf(insn, 9, 5);
		int rt = bf(insn, 4, 0);
		int rs = bf(insn, 20, 16);
		const char *name;
		const char *rt_n;

		switch (size_f) {
		case 0: name = o0 ? "ldxrb" : "stxrb"; rt_n = wreg[rt]; break;
		case 1: name = o0 ? "ldxrh" : "stxrh"; rt_n = wreg[rt]; break;
		case 2: name = o0 ? "ldxr"  : "stxr";  rt_n = wreg[rt]; break;
		case 3: name = o0 ? "ldxr"  : "stxr";  rt_n = xreg[rt]; break;
		default: goto not_excl;
		}

		sb_init(&sb, ops, sizeof ops);
		if (o0) {
			sb_s(&sb, rt_n);
			sb_s(&sb, ", [");
			sb_s(&sb, rn_sp(rn_r, 1));
			sb_s(&sb, "]");
		} else {
			sb_s(&sb, wreg[rs]);
			sb_s(&sb, ", ");
			sb_s(&sb, rt_n);
			sb_s(&sb, ", [");
			sb_s(&sb, rn_sp(rn_r, 1));
			sb_s(&sb, "]");
		}
		finish(out, bytes, size, offset, addr, name, ops);
		return 0;
		not_excl:;
	}

	/* ============================================================== */
	/* Unknown instruction -> (bad)                                     */
	/* ============================================================== */
	goto bad;
}
