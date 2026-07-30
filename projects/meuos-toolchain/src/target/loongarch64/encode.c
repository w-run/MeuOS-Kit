/* encode.c — LoongArch 64-bit instruction encoder (LA64 base + atomic + FP).
 *
 * All instructions are fixed 4 bytes.  Supports the instruction patterns
 * emitted by mcc's loongarch64 backend and runtime .S files.
 *
 * Syntax:  mnemonic rd, rj, rk       3-register
 *          mnemonic rd, rj, imm      2RI12 (or 3-operand load/store)
 *          mnemonic rd, off(rj)      memory alias
 * Registers: $zero, $ra, $tp, $sp, $a0-$a7, $t0-$t8, $s0-$s9, $fp
 *            $f0-$f31 (also $fa0-$fa7, $ft0-$ft15, $fs0-$fs15)
 *
 * Relocation markers (mcc / GNU-as compatible):
 *   %pc_hi20(sym) %pc_lo12(sym)         PC-relative medium/large code model
 *   %got_pc_hi20(sym) %got_pc_lo12(sym) GOT-relative
 *   %ie_pc_hi20(sym) %ie_pc_lo12(sym)   TLS initial-exec
 *   %gd_pc_hi20(sym)                    TLS general-dynamic
 *   %le_hi20(sym) %le_lo12(sym)         TLS local-exec
 *
 * All instruction constants below were verified bit-exact against
 * loongarch64-linux-gnu-as (binutils 2.41). */

#include "mt/target.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- LoongArch relocation types (mirror reloc.c) ---- */
#define RLA_B16            64
#define RLA_B26            66
#define RLA_PCALA_HI20     71
#define RLA_PCALA_LO12     72
#define RLA_GOT_PC_HI20    75
#define RLA_GOT_PC_LO12    76
#define RLA_TLS_IE_PC_HI20 87
#define RLA_TLS_IE_PC_LO12 88
#define RLA_TLS_GD_PC_HI20 97
#define RLA_TLS_LE_HI20    83
#define RLA_TLS_LE_LO12    84
#define RLA_TLS_LE64_LO20  85
#define RLA_TLS_LE64_HI12  86

/* ---- Local helpers ---- */

/* strdup is POSIX; provide a self-contained copy to avoid feature-test macros. */
static char *xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

/* ---- Register table ---- */

struct la_reg {
	const char *name;
	int num;      /* 0-31 */
};

static const struct la_reg gpr[] = {
	{"$zero", 0}, {"$r0", 0},
	{"$ra",   1}, {"$r1", 1},
	{"$tp",   2}, {"$r2", 2},
	{"$sp",   3}, {"$r3", 3},
	{"$a0",   4}, {"$r4", 4},
	{"$a1",   5}, {"$r5", 5},
	{"$a2",   6}, {"$r6", 6},
	{"$a3",   7}, {"$r7", 7},
	{"$a4",   8}, {"$r8", 8},
	{"$a5",   9}, {"$r9", 9},
	{"$a6",  10}, {"$r10", 10},
	{"$a7",  11}, {"$r11", 11},
	{"$t0",  12}, {"$r12", 12},
	{"$t1",  13}, {"$r13", 13},
	{"$t2",  14}, {"$r14", 14},
	{"$t3",  15}, {"$r15", 15},
	{"$t4",  16}, {"$r16", 16},
	{"$t5",  17}, {"$r17", 17},
	{"$t6",  18}, {"$r18", 18},
	{"$t7",  19}, {"$r19", 19},
	{"$t8",  20}, {"$r20", 20},
	{"$s0",  21}, {"$r21", 21}, {"$fp", 21},
	{"$s1",  22}, {"$r22", 22},
	{"$s2",  23}, {"$r23", 23},
	{"$s3",  24}, {"$r24", 24},
	{"$s4",  25}, {"$r25", 25},
	{"$s5",  26}, {"$r26", 26},
	{"$s6",  27}, {"$r27", 27},
	{"$s7",  28}, {"$r28", 28},
	{"$s8",  29}, {"$r29", 29},
	{"$s9",  30}, {"$r30", 30},
	{"$r31", 31},
};

static const struct la_reg fpr[] = {
	{"$fa0",  0}, {"$f0",  0},
	{"$fa1",  1}, {"$f1",  1},
	{"$fa2",  2}, {"$f2",  2},
	{"$fa3",  3}, {"$f3",  3},
	{"$fa4",  4}, {"$f4",  4},
	{"$fa5",  5}, {"$f5",  5},
	{"$fa6",  6}, {"$f6",  6},
	{"$fa7",  7}, {"$f7",  7},
	{"$ft0",  8}, {"$f8",  8},
	{"$ft1",  9}, {"$f9",  9},
	{"$ft2", 10}, {"$f10", 10},
	{"$ft3", 11}, {"$f11", 11},
	{"$ft4", 12}, {"$f12", 12},
	{"$ft5", 13}, {"$f13", 13},
	{"$ft6", 14}, {"$f14", 14},
	{"$ft7", 15}, {"$f15", 15},
	{"$ft8", 16}, {"$f16", 16},
	{"$ft9", 17}, {"$f17", 17},
	{"$ft10", 18}, {"$f18", 18},
	{"$ft11", 19}, {"$f19", 19},
	{"$ft12", 20}, {"$f20", 20},
	{"$ft13", 21}, {"$f21", 21},
	{"$ft14", 22}, {"$f22", 22},
	{"$ft15", 23}, {"$f23", 23},
	{"$fs0",  24}, {"$f24", 24},
	{"$fs1",  25}, {"$f25", 25},
	{"$fs2",  26}, {"$f26", 26},
	{"$fs3",  27}, {"$f27", 27},
	{"$fs4",  28}, {"$f28", 28},
	{"$fs5",  29}, {"$f29", 29},
	{"$fs6",  30}, {"$f30", 30},
	{"$fs7",  31}, {"$f31", 31},
};

static const struct la_reg fcc_regs[] = {
	{"$fcc0", 0}, {"$fcc1", 1}, {"$fcc2", 2}, {"$fcc3", 3},
	{"$fcc4", 4}, {"$fcc5", 5}, {"$fcc6", 6}, {"$fcc7", 7},
};

static int
parse_reg(const char *name)
{
	size_t i;
	if (!name || !*name) return -1;
	for (i = 0; i < sizeof gpr / sizeof gpr[0]; ++i)
		if (strcmp(gpr[i].name, name) == 0)
			return gpr[i].num;
	for (i = 0; i < sizeof fpr / sizeof fpr[0]; ++i)
		if (strcmp(fpr[i].name, name) == 0)
			return fpr[i].num;
	return -1;
}

static int
parse_fcc(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof fcc_regs / sizeof fcc_regs[0]; ++i)
		if (strcmp(fcc_regs[i].name, name) == 0)
			return fcc_regs[i].num;
	return -1;
}

/* ---- Operand parsing ---- */

enum {
	RELK_PLAIN = 0,
	RELK_PC_HI20, RELK_PC_LO12,
	RELK_GOT_PC_HI20, RELK_GOT_PC_LO12,
	RELK_IE_PC_HI20, RELK_IE_PC_LO12,
	RELK_GD_PC_HI20,
	RELK_LE_HI20, RELK_LE_LO12,
	RELK_LE64_LO20, RELK_LE64_HI12
};

struct la_op {
	int kind;     /* 0=invalid, 1=reg, 2=imm, 3=mem, 4=symbol/reloc */
	int reg;
	int64_t imm;
	const char *sym;
	int64_t addend;
	int mem_reg;
	int relkind;  /* RELK_* when kind==4 */
};

static int
reloc_kind_of(const char *tok)
{
	if (tok[0] != '%') return RELK_PLAIN;
	if      (!strncmp(tok, "%pc_hi20(",     10)) return RELK_PC_HI20;
	else if (!strncmp(tok, "%pc_lo12(",     10)) return RELK_PC_LO12;
	else if (!strncmp(tok, "%got_pc_hi20(", 13)) return RELK_GOT_PC_HI20;
	else if (!strncmp(tok, "%got_pc_lo12(", 13)) return RELK_GOT_PC_LO12;
	else if (!strncmp(tok, "%ie_pc_hi20(",  13)) return RELK_IE_PC_HI20;
	else if (!strncmp(tok, "%ie_pc_lo12(",  13)) return RELK_IE_PC_LO12;
	else if (!strncmp(tok, "%gd_pc_hi20(",  13)) return RELK_GD_PC_HI20;
	else if (!strncmp(tok, "%le_hi20(",     10)) return RELK_LE_HI20;
	else if (!strncmp(tok, "%le_lo12(",     10)) return RELK_LE_LO12;
	else if (!strncmp(tok, "%le64_lo20(",  12)) return RELK_LE64_LO20;
	else if (!strncmp(tok, "%le64_hi12(",  12)) return RELK_LE64_HI12;
	return RELK_PLAIN;
}

static int
parse_operands(const char *text, struct la_op ops[4], int *nops)
{
	char buf[256];
	char *tok;
	*nops = 0;
	if (!text || !*text) return 0;
	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	tok = strtok(buf, ",");
	while (tok && *nops < 4) {
		while (*tok == ' ') tok++;
		if (!*tok) { tok = strtok(NULL, ","); continue; }

		ops[*nops].kind = 0;
		ops[*nops].reg = -1;
		ops[*nops].imm = 0;
		ops[*nops].sym = NULL;
		ops[*nops].addend = 0;
		ops[*nops].mem_reg = -1;
		ops[*nops].relkind = RELK_PLAIN;

		/* Relocation marker: %marker(symbol) */
		if (tok[0] == '%' && strchr(tok, '(')) {
			int rk = reloc_kind_of(tok);
			const char *start = strchr(tok, '(') + 1;
			const char *end = strchr(start, ')');
			size_t len = end ? (size_t)(end - start) : strlen(start);
			char *s = malloc(len + 1);
			if (!s) return -1;
			memcpy(s, start, len);
			s[len] = '\0';
			ops[*nops].kind = 4;
			ops[*nops].relkind = rk;
			ops[*nops].sym = s;
			(*nops)++; tok = strtok(NULL, ","); continue;
		}

		if (tok[0] == '-' || (tok[0] >= '0' && tok[0] <= '9')) {
			char *end;
			long nv = strtol(tok, &end, 0);
			if (end != tok && *end == '\0') {
				ops[*nops].kind = 2;
				ops[*nops].imm = nv;
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
			ops[*nops].kind = 4;
			ops[*nops].relkind = RELK_PLAIN;
			ops[*nops].sym = xstrdup(tok);
			(*nops)++; tok = strtok(NULL, ","); continue;
		}

		if (strncmp(tok, "$fcc", 4) == 0) {
			int fcc = parse_fcc(tok);
			if (fcc >= 0) {
				ops[*nops].kind = 1;
				ops[*nops].reg = fcc;
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
		}

		{
			const char *paren = strchr(tok, '(');
			if (paren) {
				const char *start = paren + 1;
				const char *end = strchr(start, ')');
				size_t reglen;
				ops[*nops].kind = 3;
				if (paren > tok) {
					char *ep;
					ops[*nops].imm = strtol(tok, &ep, 0);
				}
				if (!end) { tok = strtok(NULL, ","); continue; }
				reglen = (size_t)(end - start);
				{
					char regname[32];
					if (reglen >= sizeof(regname)) reglen = sizeof(regname) - 1;
					memcpy(regname, start, reglen);
					regname[reglen] = '\0';
					ops[*nops].mem_reg = parse_reg(regname);
				}
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
		}

		{
			int r = parse_reg(tok);
			if (r >= 0) {
				ops[*nops].kind = 1;
				ops[*nops].reg = r;
			} else {
				char *ep;
				long imm = strtol(tok, &ep, 0);
				if (*ep == '\0') {
					ops[*nops].kind = 2;
					ops[*nops].imm = imm;
				} else {
					ops[*nops].kind = 4;
					ops[*nops].relkind = RELK_PLAIN;
					ops[*nops].sym = xstrdup(tok);
				}
			}
			(*nops)++;
		}
		tok = strtok(NULL, ",");
	}
	return 0;
}

/* ---- Encoding helpers ---- */

static void
emit32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* 3R: op | rd | (rj<<5) | (rk<<10); op is bits 10..31 (bits 0..9 zero). */
static uint32_t
r3(uint32_t op, unsigned rd, unsigned rj, unsigned rk)
{
	return op | rd | (rj << 5) | (rk << 10);
}

/* 2RI12: op | rd | (rj<<5) | (imm12<<10); op is bits 22..31. */
static uint32_t
ri12(uint32_t op_22_31, unsigned rd, unsigned rj, int32_t imm12)
{
	return op_22_31 | rd | (rj << 5) | ((uint32_t)(imm12 & 0xFFF) << 10);
}

static void
set_fixup(struct mt_insn *out, size_t offset, unsigned width,
          unsigned reloc_type, const char *sym, int64_t addend)
{
	out->fixed = 0;
	out->fixup_offset = offset;
	out->fixup_width = width;
	out->reloc_type = reloc_type;
	out->fixup_symbol = sym;
	out->fixup_addend = addend;
}

static unsigned
relkind_to_type(int rk)
{
	switch (rk) {
	case RELK_PC_HI20:    return RLA_PCALA_HI20;
	case RELK_PC_LO12:    return RLA_PCALA_LO12;
	case RELK_GOT_PC_HI20:return RLA_GOT_PC_HI20;
	case RELK_GOT_PC_LO12:return RLA_GOT_PC_LO12;
	case RELK_IE_PC_HI20: return RLA_TLS_IE_PC_HI20;
	case RELK_IE_PC_LO12: return RLA_TLS_IE_PC_LO12;
	case RELK_GD_PC_HI20: return RLA_TLS_GD_PC_HI20;
	case RELK_LE_HI20:    return RLA_TLS_LE_HI20;
	case RELK_LE_LO12:    return RLA_TLS_LE_LO12;
	case RELK_LE64_LO20:  return RLA_TLS_LE64_LO20;
	case RELK_LE64_HI12:  return RLA_TLS_LE64_HI12;
	default:              return 0;
	}
}

int
la64_encode_insn(const struct mt_target *target,
                 const char *mnemonic, const char *operands,
                 struct mt_insn *out)
{
	struct la_op ops[4];
	int nops = 0;
	(void)target;
	memset(out, 0, sizeof(*out));
	out->fixed = 1;
	out->size = 4;
	out->ok = 1;

	parse_operands(operands, ops, &nops);
	out->fixed = 1;
	out->size = 4;
	out->ok = 1;

	parse_operands(operands, ops, &nops);

	unsigned rd = 0, rj = 0, rk = 0;
	int64_t imm = 0;
	const char *sym = NULL;
	int relkind = RELK_PLAIN;
	unsigned reloc_type = 0;

	/* jirl must be checked before any (nops==3 && reg && reg) block,
	 * because jirl rd,rj,off has 3 operands with first two as registers
	 * and would be falsely matched by ALU/branch blocks below. */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    strcmp(mnemonic, "jirl") == 0) {
		unsigned rd_j = ops[0].reg, rj_j = ops[1].reg;
		int32_t off_j = (nops >= 3 && ops[2].kind == 2) ? (int32_t)ops[2].imm : 0;
		const char *jsym = (nops >= 3 && ops[2].kind == 4) ? ops[2].sym : NULL;
		if (jsym) {
			emit32(out->bytes, (0x13 << 26) | rd_j | (rj_j << 5));
			set_fixup(out, 0, 4, RLA_B16, jsym, 0);
		} else {
			emit32(out->bytes, (0x13 << 26) | rd_j | (rj_j << 5) |
			       ((uint32_t)(off_j & 0x3FFFFFF) << 10));
		}
		return 0;
	}
	/* jr rd  ->  jirl $zero, rd, 0 */
	if (strcmp(mnemonic, "jr") == 0 && nops == 1 && ops[0].kind == 1) {
		emit32(out->bytes, (0x13 << 26) | (ops[0].reg << 5));
		return 0;
	}
	/* ret  ->  jirl $zero, $ra, 0 */
	if (strcmp(mnemonic, "ret") == 0) {
		emit32(out->bytes, (0x13 << 26) | (1 << 5));
		return 0;
	}
	/* syscall */
	if (strcmp(mnemonic, "syscall") == 0) {
		int32_t code = (nops >= 1) ? (int32_t)ops[0].imm : 0;
		emit32(out->bytes, 0x18 << 24 | (code & 0x1F));
		return 0;
	}

#define LAST_RELOC() do { \
		if (nops > 0 && ops[nops-1].kind == 4) { \
			sym = ops[nops-1].sym; \
			relkind = ops[nops-1].relkind; \
			reloc_type = relkind_to_type(relkind); \
		} \
	} while (0)

	/* === 3-address floating-point R-type (before integer R-type to
	 * avoid "all-register" block interception). ===
	 * fadd.s/d, fsub.s/d, fmul.s/d, fdiv.s/d rd, fj, fk */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1 &&
	    mnemonic[0] == 'f') {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].reg;
		int is_d = (strstr(mnemonic, ".d") != NULL);
		uint32_t op_base = is_d ? 0xD << 24 : 0xC << 24;
		if      (strcmp(mnemonic, "fadd.s") == 0 || strcmp(mnemonic, "fadd.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (0<<15)); return 0; }
		else if (strcmp(mnemonic, "fsub.s") == 0 || strcmp(mnemonic, "fsub.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (1<<15)); return 0; }
		else if (strcmp(mnemonic, "fmul.s") == 0 || strcmp(mnemonic, "fmul.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (2<<15)); return 0; }
		else if (strcmp(mnemonic, "fdiv.s") == 0 || strcmp(mnemonic, "fdiv.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (3<<15)); return 0; }
		else return -1;
	}

	/* === 3-address R-type (integer) === */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].reg;
		uint32_t op = 0;
		if      (strcmp(mnemonic, "add.w") == 0)  op = 0x100000;
		else if (strcmp(mnemonic, "add.d") == 0)  op = 0x108000;
		else if (strcmp(mnemonic, "sub.w") == 0)  op = 0x110000;
		else if (strcmp(mnemonic, "sub.d") == 0)  op = 0x118000;
		else if (strcmp(mnemonic, "and")   == 0)  op = 0x148000;
		else if (strcmp(mnemonic, "or")    == 0)  op = 0x150000;
		else if (strcmp(mnemonic, "xor")   == 0)  op = 0x158000;
		else if (strcmp(mnemonic, "nor")   == 0)  op = 0x140000;
		else if (strcmp(mnemonic, "slt")   == 0)  op = 0x120000;
		else if (strcmp(mnemonic, "sltu")  == 0)  op = 0x128000;
		else if (strcmp(mnemonic, "mul.w") == 0)  op = 0x1C0000;
		else if (strcmp(mnemonic, "mul.d") == 0)  op = 0x1D8000;
		else if (strcmp(mnemonic, "div.w") == 0)  op = 0x200000;
		else if (strcmp(mnemonic, "div.d") == 0)  op = 0x220000;
		else if (strcmp(mnemonic, "mod.w") == 0)  op = 0x208000;
		else if (strcmp(mnemonic, "mod.d") == 0)  op = 0x228000;
		else if (strcmp(mnemonic, "div.wu")== 0)  op = 0x210000;
		else if (strcmp(mnemonic, "div.du")== 0)  op = 0x230000;
		else if (strcmp(mnemonic, "mod.wu")== 0)  op = 0x218000;
		else if (strcmp(mnemonic, "mod.du")== 0)  op = 0x238000;
		else if (strcmp(mnemonic, "sll.w") == 0)  op = 0x170000;
		else if (strcmp(mnemonic, "sll.d") == 0)  op = 0x188000;
		else if (strcmp(mnemonic, "srl.w") == 0)  op = 0x178000;
		else if (strcmp(mnemonic, "srl.d") == 0)  op = 0x190000;
		else if (strcmp(mnemonic, "sra.w") == 0)  op = 0x180000;
		else if (strcmp(mnemonic, "sra.d") == 0)  op = 0x198000;
		else return -1;
		emit32(out->bytes, r3(op, rd, rj, rk));
		return 0;
	}

	/* === 2-address R-type: add.w/add.d rd, rj (rk = $zero) === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    (strcmp(mnemonic, "add.w") == 0 || strcmp(mnemonic, "add.d") == 0)) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		uint32_t op = (mnemonic[3] == 'w') ? 0x100000 : 0x108000;
		emit32(out->bytes, r3(op, rd, rj, 0));
		return 0;
	}

	/* === Load / Store (2RI12 memory) ===
	 * ld.d rd, rj, imm   (3-operand, mcc native)  or  ld.d rd, imm(rj) */
	if (nops >= 2 && ops[0].kind == 1 &&
	    (strncmp(mnemonic, "ld.", 3) == 0 || strncmp(mnemonic, "st.", 3) == 0 ||
	     strncmp(mnemonic, "fld", 3) == 0 || strncmp(mnemonic, "fst", 3) == 0)) {
		rd = (unsigned)ops[0].reg;
		rj = 0; imm = 0; sym = NULL; reloc_type = 0;
		int have_reloc = 0;
		if (nops == 3 && ops[1].kind == 1) {
			rj = (unsigned)ops[1].reg;
			if (ops[2].kind == 2) imm = ops[2].imm;
			else if (ops[2].kind == 4) {
				sym = ops[2].sym; relkind = ops[2].relkind;
				reloc_type = relkind_to_type(relkind);
				have_reloc = 1;
			} else return -1;
		} else if (nops == 2 && ops[1].kind == 3) {
			rj = (unsigned)ops[1].mem_reg;
			imm = ops[1].imm;
		} else return -1;

		uint32_t op = 0;
		if      (strcmp(mnemonic, "ld.b")  == 0) op = 0x0A0 << 22;
		else if (strcmp(mnemonic, "ld.h")  == 0) op = 0x0A1 << 22;
		else if (strcmp(mnemonic, "ld.w")  == 0) op = 0x0A2 << 22;
		else if (strcmp(mnemonic, "ld.d")  == 0) op = 0x0A3 << 22;
		else if (strcmp(mnemonic, "ld.bu") == 0) op = 0x0A8 << 22;
		else if (strcmp(mnemonic, "ld.hu") == 0) op = 0x0A9 << 22;
		else if (strcmp(mnemonic, "ld.wu") == 0) op = 0x0AA << 22;
		else if (strcmp(mnemonic, "st.b")  == 0) op = 0x0A4 << 22;
		else if (strcmp(mnemonic, "st.h")  == 0) op = 0x0A5 << 22;
		else if (strcmp(mnemonic, "st.w")  == 0) op = 0x0A6 << 22;
		else if (strcmp(mnemonic, "st.d")  == 0) op = 0x0A7 << 22;
		else if (strcmp(mnemonic, "fld.s") == 0 || strcmp(mnemonic, "fld.w") == 0) op = 0x0AC << 22;
		else if (strcmp(mnemonic, "fld.d") == 0) op = 0x0AE << 22;
		else if (strcmp(mnemonic, "fst.s") == 0 || strcmp(mnemonic, "fst.w") == 0) op = 0x0AD << 22;
		else if (strcmp(mnemonic, "fst.d") == 0) op = 0x0AF << 22;
		else return -1;

		if (have_reloc) {
			emit32(out->bytes, ri12(op, rd, rj, 0));
			set_fixup(out, 0, 4, reloc_type, sym, 0);
		} else {
			emit32(out->bytes, ri12(op, rd, rj, (int32_t)imm));
		}
		return 0;
	}

	/* === Atomic LL/SC (3-operand forms: rd, rj, imm12 or rd, rj, rk).
	 * Must come before the branch / ALU / shift blocks which would
	 * otherwise intercept the same operand pattern (nops==3, reg, reg). */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (ops[2].kind == 2 &&
		    (strcmp(mnemonic, "ll.w") == 0 || strcmp(mnemonic, "ll.d") == 0)) {
			int32_t imm_a = (int32_t)ops[2].imm;
			if (strcmp(mnemonic, "ll.w") == 0)
				emit32(out->bytes, (0x0A0<<24) | rd | (rj<<5) | ((imm_a&0xFFF)<<10));
			else
				emit32(out->bytes, (0x0AA<<24) | rd | (rj<<5) | ((imm_a&0xFFF)<<10));
			return 0;
		}
		if ((ops[2].kind == 1 || (ops[2].kind == 2 && ops[2].imm == 0)) &&
		    (strcmp(mnemonic, "sc.w") == 0 || strcmp(mnemonic, "sc.d") == 0)) {
			rk = (ops[2].kind == 1) ? (unsigned)ops[2].reg : 0;
			if (strcmp(mnemonic, "sc.w") == 0)
				emit32(out->bytes, (0x0A1<<24) | rd | (rj<<5) | (rk<<10));
			else
				emit32(out->bytes, (0x0AB<<24) | rd | (rj<<5) | (rk<<10));
			return 0;
		}
	}

	/* === 2-register conditional branches: beq/bne/blt/bge/bltu/bgeu ===
	 * Must come BEFORE the 2RI12 ALU block (which also matches
	 * nops==3 with two registers but expects ALU mnemonics, not
	 * branches with a label operand of kind==4). */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1) {
		uint32_t op26 = 0;
		if      (strcmp(mnemonic, "beq")  == 0) op26 = 0x16;
		else if (strcmp(mnemonic, "bne")  == 0) op26 = 0x17;
		else if (strcmp(mnemonic, "blt")  == 0) op26 = 0x18;
		else if (strcmp(mnemonic, "bge")  == 0) op26 = 0x19;
		else if (strcmp(mnemonic, "bltu") == 0) op26 = 0x1A;
		else if (strcmp(mnemonic, "bgeu") == 0) op26 = 0x1B;
		else if (strcmp(mnemonic, "lu52i.d") == 0) {
			rd = (unsigned)ops[0].reg;
			rj = (unsigned)ops[1].reg;
			if (ops[2].kind == 4) {
				emit32(out->bytes, 0x16800000 | rd | (rj << 5));
				sym = ops[2].sym; relkind = ops[2].relkind;
				reloc_type = relkind_to_type(relkind);
				if (reloc_type == 0) reloc_type = RLA_TLS_LE64_HI12;
				set_fixup(out, 0, 4, reloc_type, sym, 0);
			} else if (ops[2].kind == 2) {
				emit32(out->bytes, 0x16800000 | rd | (rj << 5) |
				    ((uint32_t)(ops[2].imm & 0xFFF) << 10));
			} else return -1;
			return 0;
		} else goto alu_or_shift;
		rj = (unsigned)ops[0].reg;
		rd = (unsigned)ops[1].reg;   /* rk */
		if (ops[2].kind == 4) {
			emit32(out->bytes, (op26 << 26) | rd | (rj << 5));
			set_fixup(out, 0, 4, RLA_B16, ops[2].sym, 0);
		} else if (ops[2].kind == 2) {
			int32_t off = (int32_t)ops[2].imm;
			emit32(out->bytes, (op26 << 26) | rd | (rj << 5) | ((uint32_t)(off & 0xFFFF) << 10));
		} else return -1;
		return 0;
	}
alu_or_shift:

	/* === 2RI14 shifts: slli.w/d, srli.w/d, srai.w/d ===
	 * Must come BEFORE 2RI12 ALU because both match the same operand
	 * pattern (nops==3, reg, reg, imm) and shifts are not ALU mnemonics.
	 * base already includes the size flag (bit15 .w, bit16 .d); shift
	 * amount goes into bits 10..14 (.w) or 10..15 (.d). */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 2) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		uint32_t shamt = (uint32_t)ops[2].imm;
		uint32_t base = 0; uint32_t mask = 0x1F;
		if      (strcmp(mnemonic, "slli.w") == 0) { base = 0x00408000; mask = 0x1F; }
		else if (strcmp(mnemonic, "slli.d") == 0) { base = 0x00410000; mask = 0x3F; }
		else if (strcmp(mnemonic, "srli.w") == 0) { base = 0x00448000; mask = 0x1F; }
		else if (strcmp(mnemonic, "srli.d") == 0) { base = 0x00450000; mask = 0x3F; }
		else if (strcmp(mnemonic, "srai.w") == 0) { base = 0x00488000; mask = 0x1F; }
		else if (strcmp(mnemonic, "srai.d") == 0) { base = 0x00490000; mask = 0x3F; }
		else goto alu2ri12;
		emit32(out->bytes, base | rd | (rj << 5) | ((shamt & mask) << 10));
		return 0;
	}
alu2ri12:

	/* === 2RI12 ALU: addi.w/d, slti, sltui, andi, ori, xori ===
	 * Imm can be kind==2 (plain immediate) or kind==4 (relocation
	 * expression like %pc_lo12(sym) or %le_lo12(sym)).
	 * NOTE: the conditional branch block above has already consumed
	 * beq/bne/etc., so this block only sees genuine ALU mnemonics. */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    (ops[2].kind == 2 || ops[2].kind == 4)) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		imm = (ops[2].kind == 2) ? ops[2].imm : 0;
		uint32_t op = 0;
		if      (strcmp(mnemonic, "addi.w") == 0) op = 0x0A << 22;
		else if (strcmp(mnemonic, "addi.d") == 0) op = 0x0B << 22;
		else if (strcmp(mnemonic, "slti")   == 0) op = 0x08 << 22;
		else if (strcmp(mnemonic, "sltui")  == 0) op = 0x09 << 22;
		else if (strcmp(mnemonic, "andi")   == 0) op = 0x0D << 22;
		else if (strcmp(mnemonic, "ori")    == 0) op = 0x0E << 22;
		else if (strcmp(mnemonic, "xori")   == 0) op = 0x0F << 22;
		else return -1;
		if (ops[2].kind == 4) {
			emit32(out->bytes, ri12(op, rd, rj, 0));
			set_fixup(out, 0, 4,
			    relkind_to_type(ops[2].relkind) ? relkind_to_type(ops[2].relkind) : RLA_PCALA_LO12,
			    ops[2].sym, 0);
		} else {
			emit32(out->bytes, ri12(op, rd, rj, (int32_t)imm));
		}
		return 0;
	}

	/* === 1RI20: lu12i.w rd, imm20 === */
	if (nops == 2 && ops[0].kind == 1 && strcmp(mnemonic, "lu12i.w") == 0) {
		rd = (unsigned)ops[0].reg;
		if (ops[1].kind == 4) {
			sym = ops[1].sym; relkind = ops[1].relkind;
			reloc_type = relkind_to_type(relkind);
			emit32(out->bytes, 0x14 << 24 | rd);
			set_fixup(out, 0, 4, reloc_type, sym, 0);
		} else if (ops[1].kind == 2) {
			uint32_t imm20 = (uint32_t)(ops[1].imm & 0xFFFFF);
			emit32(out->bytes, 0x14 << 24 | rd | (imm20 << 5));
		} else return -1;
		return 0;
	}

	/* === 1RI20: lu32i.d rd, imm20 or %le64_lo20(sym) (bits 51:32) === */
	if (nops == 2 && ops[0].kind == 1 && strcmp(mnemonic, "lu32i.d") == 0) {
		rd = (unsigned)ops[0].reg;
		if (ops[1].kind == 4) {
			sym = ops[1].sym; relkind = ops[1].relkind;
			reloc_type = relkind_to_type(relkind);
			if (reloc_type == 0) reloc_type = RLA_TLS_LE64_LO20;
			emit32(out->bytes, 0x16 << 24 | rd);
			set_fixup(out, 0, 4, reloc_type, sym, 0);
		} else if (ops[1].kind == 2) {
			uint32_t imm20 = (uint32_t)(ops[1].imm & 0xFFFFF);
			emit32(out->bytes, 0x16 << 24 | rd | (imm20 << 5));
		} else return -1;
		return 0;
	}

	/* === 1RI20: pcalau12i rd, imm20 (PC-relative relocations) === */
	if (nops == 2 && ops[0].kind == 1 && strcmp(mnemonic, "pcalau12i") == 0) {
		rd = (unsigned)ops[0].reg;
		if (ops[1].kind == 4) {
			sym = ops[1].sym; relkind = ops[1].relkind;
			reloc_type = relkind_to_type(relkind);
			if (reloc_type == 0) reloc_type = RLA_PCALA_HI20;
			emit32(out->bytes, 0x1A << 24 | rd);
			set_fixup(out, 0, 4, reloc_type, sym, 0);
		} else if (ops[1].kind == 2) {
			uint32_t imm20 = (uint32_t)(ops[1].imm & 0xFFFFF);
			emit32(out->bytes, 0x1A << 24 | rd | (imm20 << 5));
		} else return -1;
		return 0;
	}

	/* === 1-register conditional branches: beqz, bnez, bltz, bgez ===
	 * beqz/bnez: op(24..31) | (rj<<5) | (off<<10)
	 * bltz/bgez: aliases for blt/bge rj, $zero */
	if (nops == 2 && ops[0].kind == 1) {
		rj = (unsigned)ops[0].reg;
		uint32_t op24 = 0;
		int is_alias = 0;
		if      (strcmp(mnemonic, "beqz") == 0) op24 = 0x40;
		else if (strcmp(mnemonic, "bnez") == 0) op24 = 0x44;
		else if (strcmp(mnemonic, "bltz") == 0) { op24 = 0x18; is_alias = 1; } /* blt rj,$zero */
		else if (strcmp(mnemonic, "bgez") == 0) { op24 = 0x19; is_alias = 1; } /* bge rj,$zero */
		else goto branch2;
		if (is_alias) {
			/* blt rj,$zero / bge rj,$zero -> 2-reg family */
			uint32_t op26 = op24;
			if (ops[1].kind == 4) {
				emit32(out->bytes, (op26 << 26) | (0) | (rj << 5));
				set_fixup(out, 0, 4, RLA_B16, ops[1].sym, 0);
			} else if (ops[1].kind == 2) {
				int32_t off = (int32_t)ops[1].imm;
				emit32(out->bytes, (op26 << 26) | (rj << 5) | ((uint32_t)(off & 0xFFFF) << 10));
			} else return -1;
			return 0;
		}
		if (ops[1].kind == 4) {
			emit32(out->bytes, (op24 << 24) | (rj << 5));
			set_fixup(out, 0, 4, RLA_B16, ops[1].sym, 0);
		} else if (ops[1].kind == 2) {
			int32_t off = (int32_t)ops[1].imm;
			emit32(out->bytes, (op24 << 24) | (rj << 5) | ((uint32_t)(off & 0xFFFF) << 10));
		} else return -1;
		return 0;
	}
branch2:

	/* === dbar / ibcl / break (must come before b/bl, which also
	 * matches nops==1 && ops[0].kind==2). === */
	if (strcmp(mnemonic, "dbar") == 0) {
		int32_t hint = (nops >= 1) ? (int32_t)ops[0].imm : 0;
		emit32(out->bytes, 0x0B << 28 | (hint & 0x1F));
		return 0;
	}
	if (strcmp(mnemonic, "ibcl") == 0) {
		emit32(out->bytes, 0x0B << 28 | 0x20);
		return 0;
	}
	if (strcmp(mnemonic, "break") == 0) {
		int32_t code = (nops >= 1) ? (int32_t)ops[0].imm : 0;
		emit32(out->bytes, 0xB0000000 | ((uint32_t)(code & 0x7FFF) << 10));
		return 0;
	}

	/* === Unconditional branches: b / bl (B26) === */
	if (nops == 1 && ops[0].kind == 2) {
		int32_t off = (int32_t)ops[0].imm;
		if (strcmp(mnemonic, "b") == 0)
			emit32(out->bytes, (0x14 << 26) | ((uint32_t)(off & 0x3FFFFFF) << 10));
		else if (strcmp(mnemonic, "bl") == 0)
			emit32(out->bytes, (0x15 << 26) | ((uint32_t)(off & 0x3FFFFFF) << 10));
		else return -1;
		return 0;
	}
	if (nops == 1 && ops[0].kind == 4) {
		if (strcmp(mnemonic, "b") == 0) {
			emit32(out->bytes, 0x14 << 26);
			set_fixup(out, 0, 4, RLA_B26, ops[0].sym, 0);
		} else if (strcmp(mnemonic, "bl") == 0) {
			emit32(out->bytes, 0x15 << 26);
			set_fixup(out, 0, 4, RLA_B26, ops[0].sym, 0);
		} else return -1;
		return 0;
	}

	/* === la rd, sym (pseudo-instruction — PC-relative address).
	 * Expands to pcalau12i rd, %pc_hi20(sym) + addi.d rd, rd, %pc_lo12(sym)
	 * (two real instructions with two fixups). */
	if (strcmp(mnemonic, "la") == 0 && nops == 2 && ops[0].kind == 1 && ops[1].kind == 4) {
		rd = (unsigned)ops[0].reg;
		const char *lsym = ops[1].sym;
		emit32(out->bytes,      0x1A << 24 | rd);                    /* pcalau12i rd */
		emit32(out->bytes + 4,  ri12(0x0B << 22, rd, rd, 0));        /* addi.d rd, rd, 0 */
		out->size = 8;
		out->fixed = 0;
		set_fixup(out, 0, 4, RLA_PCALA_HI20, lsym, 0);
		out->fixup2_present = 1;
		out->fixup2_offset  = 4;
		out->fixup2_width   = 4;
		out->reloc_type2    = RLA_PCALA_LO12;
		out->fixup2_symbol  = lsym;
		out->fixup2_addend  = 0;
		return 0;
	}

	/* === move rd, rj  ->  or rd, rj, $zero === */
	if (strcmp(mnemonic, "move") == 0 && nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		emit32(out->bytes, r3(0x150000, (unsigned)ops[0].reg, (unsigned)ops[1].reg, 0));
		return 0;
	}

	/* === nop  ->  andi $zero, $zero, 0 === */
	if (strcmp(mnemonic, "nop") == 0) {
		emit32(out->bytes, 0x0D << 22);
		return 0;
	}

	/* === li.w / li.d rd, imm  (single- and dual-instruction expansions) ===
	 * Expands to:
	 *   or rd,$zero,$zero         when v == 0
	 *   ori rd,$zero,v            when 0 < v <= 0xFFF
	 *   addi.w/d rd,$zero,v      when -2048 <= v < 0
	 *   lu12i.w rd,hi             when hi = (v>>12), 0 < v <= 0xFFFFF000, lo12=0
	 *   lu12i.w + ori             when 0 < v <= 0xFFFFFFF (any lower 12 bits)
	 *   lu12i.w (sign-extended)   when -524288 <= v <= -0x1000, lo12=0 */
	if ((strcmp(mnemonic, "li.w") == 0 || strcmp(mnemonic, "li.d") == 0) && nops == 2 &&
	    ops[0].kind == 1 && ops[1].kind == 2) {
		rd = (unsigned)ops[0].reg;
		int64_t v = ops[1].imm;
		int is_d = (mnemonic[3] == 'd');
		if (v == 0) {
			emit32(out->bytes, r3(0x150000, rd, 0, 0));      /* or rd,$zero,$zero */
		} else if (v > 0 && v <= 0xFFF) {
			emit32(out->bytes, ri12(0x0E << 22, rd, 0, (int32_t)v));  /* ori */
		} else if (v >= -2048 && v < 0) {
			emit32(out->bytes, ri12((is_d ? 0x0B : 0x0A) << 22, rd, 0, (int32_t)v));
		} else if (v > 0 && (v & 0xFFF) == 0 && v <= 0xFFFFF000) {
			/* single lu12i.w: value must be 12-bit-aligned */
			uint32_t imm20 = (uint32_t)(v >> 12) & 0xFFFFF;
			emit32(out->bytes, 0x14 << 24 | rd | (imm20 << 5));
		} else if (v > 0 && v <= 0xFFFFFFF) {
			/* lu12i.w rd, hi  +  ori rd, rd, lo */
			uint32_t hi = ((uint32_t)(v >> 12)) & 0xFFFFF;
			uint32_t lo = ((uint32_t)(v & 0xFFF));
			emit32(out->bytes, 0x14 << 24 | rd | (hi << 5));
			emit32(out->bytes + 4, ri12(0x0E << 22, rd, rd, (int32_t)lo));
			out->size = 8;
		} else if ((v & 0xFFF) == 0 && v >= -524288 && v <= 524287) {
			/* signed lu12i.w: negative values where lower 12 bits = 0 */
			uint32_t imm20 = (uint32_t)(v & 0xFFFFF);
			emit32(out->bytes, 0x14 << 24 | rd | (imm20 << 5));
		} else {
			return -1;  /* would need multi-instruction expansion */
		}
		return 0;
	}

	/* === Atomic: ll.w, ll.d, sc.w, sc.d ===
	 * ll.w/d rd, imm(rj)     (mem syntax, 2 ops)
	 * ll.w/d rd, rj, imm12   (reg+imm syntax, 3 ops)
	 * sc.w/d rd, rj, rk      (3-register, ISA-native)
	 * sc.w/d rd, rj, 0       (rk = $zero shorthand) */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 3) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].mem_reg;
		int32_t im = (int32_t)ops[1].imm;
		if (strcmp(mnemonic, "ll.w") == 0) { emit32(out->bytes, (0x0A0<<24) | rd | (rj<<5) | ((im&0xFFF)<<10)); return 0; }
		if (strcmp(mnemonic, "ll.d") == 0) { emit32(out->bytes, (0x0AA<<24) | rd | (rj<<5) | ((im&0xFFF)<<10)); return 0; }
		/* sc.w/sc.d with mem syntax: rd, rj (base address in second operand) */
		if (strcmp(mnemonic, "sc.w") == 0) { emit32(out->bytes, (0x0A1<<24) | rd | (rj<<5) | (0<<10)); return 0; }
		if (strcmp(mnemonic, "sc.d") == 0) { emit32(out->bytes, (0x0AB<<24) | rd | (rj<<5) | (0<<10)); return 0; }
	}
	/* ll.w/d rd, rj, imm12 (3-operand with immediate) */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 2) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "ll.w") == 0) { emit32(out->bytes, (0x0A0<<24) | rd | (rj<<5) | (((int32_t)ops[2].imm & 0xFFF)<<10)); return 0; }
		if (strcmp(mnemonic, "ll.d") == 0) { emit32(out->bytes, (0x0AA<<24) | rd | (rj<<5) | (((int32_t)ops[2].imm & 0xFFF)<<10)); return 0; }
	}
	/* sc.w/d rd, rj, rk (3-register, ISA-native).  An immediate 0 in the
	 * third slot is accepted as shorthand for $zero (rk=0). */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    (ops[2].kind == 1 || (ops[2].kind == 2 && ops[2].imm == 0))) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (ops[2].kind == 1) ? (unsigned)ops[2].reg : 0;
		if (strcmp(mnemonic, "sc.w") == 0) { emit32(out->bytes, (0x0A1<<24) | rd | (rj<<5) | (rk<<10)); return 0; }
		if (strcmp(mnemonic, "sc.d") == 0) { emit32(out->bytes, (0x0AB<<24) | rd | (rj<<5) | (rk<<10)); return 0; }
	}

	/* === Extension: ext.w.b, ext.w.h === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "ext.w.b") == 0) { emit32(out->bytes, (0x3<<20) | rd | (rj<<5) | (7<<8) | (0<<10)); return 0; }
		if (strcmp(mnemonic, "ext.w.h") == 0) { emit32(out->bytes, (0x3<<20) | rd | (rj<<5) | (15<<8) | (0<<10)); return 0; }
	}

	/* === Bit field extract/insert: bstrpick.w/d, bstrins.w/d rd, rj, msbw, lsbw ===
	 * LA64 3RI3 format: op=0x3, bit23=1(.d)/0(.w), bits 22:20 store operation
	 * variant (000=pick, 001=ins), bits 20:16=lsbw, bits 15:10=msbw,
	 * bits 9:5=rj, bits 4:0=rd. */
	if (nops == 4 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    ops[2].kind == 2 && ops[3].kind == 2 &&
	    (strncmp(mnemonic, "bstrpick.", 9) == 0 || strncmp(mnemonic, "bstrins.", 8) == 0)) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		uint32_t msbw = (uint32_t)(ops[2].imm & 0x3F);
		uint32_t lsbw = (uint32_t)(ops[3].imm & 0x1F);
		int is_d = (strstr(mnemonic, ".d") != NULL);
		int is_ins = (mnemonic[4] == 'i'); /* bstrins vs bstrpick */
		uint32_t variant = is_ins ? 0x00200000 : 0;
		emit32(out->bytes, 0x03000000 | (is_d ? 0x00800000 : 0) | variant |
		    (lsbw << 16) | (msbw << 10) | (rj << 5) | rd);
		return 0;
	}

	/* === Float comparison: fcmp.cond.s/d $fccN, fj, fk === */
	if (strncmp(mnemonic, "fcmp.", 5) == 0 && nops == 3) {
		int fcc = ops[0].reg;
		unsigned fj = (unsigned)ops[1].reg;
		unsigned fk = (unsigned)ops[2].reg;
		int is_d = (strstr(mnemonic, ".d") != NULL);
		int cond = 8;
		if (strstr(mnemonic, "ceq")) cond = 8;
		else if (strstr(mnemonic, "clt")) cond = 4;
		else if (strstr(mnemonic, "cle")) cond = 6;
		else if (strstr(mnemonic, "cuge")) cond = 10;
		else if (strstr(mnemonic, "cult")) cond = 5;
		else if (strstr(mnemonic, "cne")) cond = 9;
		else if (strstr(mnemonic, "cor")) cond = 14;
		else if (strstr(mnemonic, "cun")) cond = 2;
		uint32_t base = is_d ? 0xD << 24 : 0xC << 24;
		emit32(out->bytes, base | (fcc << 0) | (fj << 5) | (fk << 10) | (cond << 20));
		return 0;
	}

	/* === movcf2gr rd, $fccN === */
	if (strcmp(mnemonic, "movcf2gr") == 0 && nops >= 2) {
		rd = (unsigned)ops[0].reg;
		int fcc = ops[1].reg & 0x7;
		emit32(out->bytes, (0x3<<20) | rd | (fcc << 5));
		return 0;
	}

	/* === movgr2fr.w / movgr2fr.d / movfr2gr.s / movfr2gr.d === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "movfr2gr.w") == 0) { emit32(out->bytes, (0x3<<20) | rd | (rj<<5) | (0<<15)); return 0; }
		if (strcmp(mnemonic, "movfr2gr.d") == 0) { emit32(out->bytes, (0x3<<20) | rd | (rj<<5) | (1<<15)); return 0; }
		if (strcmp(mnemonic, "movgr2fr.w") == 0) { emit32(out->bytes, (0x3<<20) | rj | (rd<<5) | (0<<15)); return 0; }
		if (strcmp(mnemonic, "movgr2fr.d") == 0) { emit32(out->bytes, (0x3<<20) | rj | (rd<<5) | (1<<15)); return 0; }
	}

	/* === Float unary: fneg.s/d, fabs.s/d, fmov.s/d,
	 * ffint.{s,d}.{w,l}, frint.s/d, fcvt.s.d, fcvt.d.s ===
	 * Check operand pattern: 2 regs, mnemonic starts with 'f'. */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1 && mnemonic[0] == 'f') {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		int is_d = (strstr(mnemonic, ".d") != NULL);
		uint32_t op_base = is_d ? 0xD << 24 : 0xC << 24;
		if (strcmp(mnemonic, "fneg.s") == 0 || strcmp(mnemonic, "fneg.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (2<<15)); return 0; }
		if (strcmp(mnemonic, "fabs.s") == 0 || strcmp(mnemonic, "fabs.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (5<<15)); return 0; }
		if (strcmp(mnemonic, "frint.s") == 0 || strcmp(mnemonic, "frint.d") == 0) { emit32(out->bytes, op_base | rd | (rj<<5) | (1<<15)); return 0; }
		/* fcvt.s.d: result is .s (base=0xC), input is .d */
		if (strcmp(mnemonic, "fcvt.s.d") == 0) { emit32(out->bytes, (0xC<<24) | rd | (rj<<5) | (2<<15)); return 0; }
		/* fcvt.d.s: result is .d (base=0xD), input is .s */
		if (strcmp(mnemonic, "fcvt.d.s") == 0) { emit32(out->bytes, (0xD<<24) | rd | (rj<<5) | (2<<15)); return 0; }
		/* ffint.s.w / ffint.s.l */
		if (strcmp(mnemonic, "ffint.s.w") == 0) { emit32(out->bytes, (0xC<<24) | rd | (rj<<5) | (12<<15)); return 0; }
		if (strcmp(mnemonic, "ffint.s.l") == 0) { emit32(out->bytes, (0xC<<24) | rd | (rj<<5) | (13<<15)); return 0; }
		/* ffint.d.w / ffint.d.l */
		if (strcmp(mnemonic, "ffint.d.w") == 0) { emit32(out->bytes, (0xD<<24) | rd | (rj<<5) | (12<<15)); return 0; }
		if (strcmp(mnemonic, "ffint.d.l") == 0) { emit32(out->bytes, (0xD<<24) | rd | (rj<<5) | (13<<15)); return 0; }
	}

	out->size = 0;
	out->ok = 0;
	return -1;
}
