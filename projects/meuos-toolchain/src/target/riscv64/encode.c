/* encode.c — RISC-V 64-bit instruction encoder (RV64I + A + F/D).

 * All instructions are fixed 4 bytes.  The encoder handles the common
 * patterns emitted by mcc's riscv64 backend and the runtime .S files.
 *
 * AT&T-ish syntax:  mnemonic rd, rs1, rs2  or  mnemonic rd, imm(rs1)
 * Registers: x0–x31 (aliases zero, ra, sp, gp, tp, t0–t6, s0–s11, a0–a7)
 *            f0–f31 (ft0–ft11, fs0–fs11, fa0–fa7) */

#include "mt/target.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- Register table ---- */

struct rv_reg {
	const char *name;
	int num;      /* 0–31 */
};

static const struct rv_reg int_regs[] = {
	{"zero",  0}, {"ra",    1}, {"sp",    2}, {"gp",    3},
	{"tp",    4}, {"t0",    5}, {"t1",    6}, {"t2",    7},
	{"s0",    8}, {"fp",    8}, {"s1",    9},
	{"a0",   10}, {"a1",   11}, {"a2",   12}, {"a3",   13},
	{"a4",   14}, {"a5",   15}, {"a6",   16}, {"a7",   17},
	{"s2",   18}, {"s3",   19}, {"s4",   20}, {"s5",   21},
	{"s6",   22}, {"s7",   23}, {"s8",   24}, {"s9",   25},
	{"s10",  26}, {"s11",  27},
	{"t3",   28}, {"t4",   29}, {"t5",   30}, {"t6",   31},
};

static const struct rv_reg fp_regs[] = {
	{"ft0",   0}, {"ft1",   1}, {"ft2",   2}, {"ft3",   3},
	{"ft4",   4}, {"ft5",   5}, {"ft6",   6}, {"ft7",   7},
	{"fs0",   8}, {"fs1",   9},
	{"fa0",  10}, {"fa1",  11}, {"fa2",  12}, {"fa3",  13},
	{"fa4",  14}, {"fa5",  15}, {"fa6",  16}, {"fa7",  17},
	{"fs2",  18}, {"fs3",  19}, {"fs4",  20}, {"fs5",  21},
	{"fs6",  22}, {"fs7",  23}, {"fs8",  24}, {"fs9",  25},
	{"fs10", 26}, {"fs11", 27},
	{"ft8",  28}, {"ft9",  29}, {"ft10", 30}, {"ft11", 31},
};

/* x0–x31 also by bare number */
static int
parse_reg(const char *name)
{
	size_t i;
	if (!name || !*name) return -1;
	if (name[0] == 'x' || name[0] == 'f') {
		char *end;
		long n = strtol(name + 1, &end, 10);
		if (*end == '\0' && n >= 0 && n <= 31)
			return (int)n;
	}
	for (i = 0; i < sizeof int_regs / sizeof int_regs[0]; ++i)
		if (strcmp(int_regs[i].name, name) == 0)
			return int_regs[i].num;
	for (i = 0; i < sizeof fp_regs / sizeof fp_regs[0]; ++i)
		if (strcmp(fp_regs[i].name, name) == 0)
			return fp_regs[i].num;
	return -1;
}

/* ---- Operand parsing ---- */

struct rv_op {
	int kind;         /* 0=invalid, 1=reg, 2=imm, 3=mem, 4=symbol */
	int reg;          /* register number */
	int64_t imm;      /* immediate value */
	const char *sym;  /* symbol name for fixups */
	int64_t addend;
	int mem_reg;      /* base register for mem operand */
};

static int
parse_operands(const char *text, struct rv_op ops[4], int *nops)
{
	char buf[256];
	char *tok;

	*nops = 0;
	if (!text || !*text)
		return 0;

	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	tok = strtok(buf, ",");
	while (tok && *nops < 4) {
		while (*tok == ' ') tok++;
		if (!*tok) { tok = strtok(NULL, ","); continue; }

		ops[*nops].kind = 1; /* reg */
		ops[*nops].reg = -1;
		ops[*nops].imm = 0;
		ops[*nops].sym = NULL;
		ops[*nops].addend = 0;
		ops[*nops].mem_reg = -1;

		/* GAS modifier form: %hi(sym) / %lo(sym). Must be checked
		 * before the memory branch because it also contains '('. */
		if (tok[0] == '%') {
			char *paren = strchr(tok, '(');
			char *endp = strchr(tok, ')');
			if (paren && endp && endp > paren) {
				*endp = '\0';
				ops[*nops].kind = 4; /* symbol */
				ops[*nops].sym = paren + 1;
				(*nops)++;
				tok = strtok(NULL, ",");
				continue;
			}
		}

		/* Memory: offset(reg) or (reg). Must be checked before the
		 * digit/'-' immediate branch because operands like 0(reg)
		 * or -16(reg) start with a digit or '-'. */
		{
			const char *paren = strchr(tok, '(');
			if (paren) {
				char regname[32];
				const char *start = paren + 1;
				const char *end = strchr(start, ')');
				size_t reglen;

				ops[*nops].kind = 3; /* mem */

				if (paren > tok) {
					char *endp;
					ops[*nops].imm = strtol(tok, &endp, 0);
					(void)endp;
				}

				if (!end) { tok = strtok(NULL, ","); continue; }
				reglen = (size_t)(end - start);
				if (reglen >= sizeof(regname)) reglen = sizeof(regname) - 1;
				memcpy(regname, start, reglen);
				regname[reglen] = '\0';
				ops[*nops].mem_reg = parse_reg(regname);

				(*nops)++;
				tok = strtok(NULL, ",");
				continue;
			}
		}

		/* Immediate: $number, $symbol, or bare number */
		if (tok[0] == '$' || tok[0] == '-' || (tok[0] >= '0' && tok[0] <= '9')) {
			const char *val = (tok[0] == '$') ? tok + 1 : tok;
			char *end;
			long nv = strtol(val, &end, 0);
			if (end != val && *end == '\0') {
				ops[*nops].kind = 2; /* imm */
				ops[*nops].imm = nv;
				(*nops)++;
				tok = strtok(NULL, ",");
				continue;
			}
			/* Symbol reference */
			ops[*nops].kind = 4; /* symbol */
			ops[*nops].sym = val;
			(*nops)++;
			tok = strtok(NULL, ",");
			continue;
		}

		/* Register, or fall back to a symbol reference (label / undefined
		 * symbol). An unrecognized token is a symbol, not an invalid reg. */
		{
			int r = parse_reg(tok);
			if (r >= 0) {
				ops[*nops].kind = 1;
				ops[*nops].reg = r;
			} else {
				ops[*nops].kind = 4; /* symbol */
				ops[*nops].sym = tok;
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

/* R-type: funct7 | rs2[4:0] | rs1[4:0] | funct3[2:0] | rd[4:0] | opcode[6:0] */
static uint32_t
r_type(uint32_t opcode, unsigned rd, unsigned funct3,
       unsigned rs1, unsigned rs2, unsigned funct7)
{
	return opcode | (rd << 7) | (funct3 << 12) |
	       (rs1 << 15) | (rs2 << 20) | (funct7 << 25);
}

/* I-type: imm12[11:0] | rs1[4:0] | funct3[2:0] | rd[4:0] | opcode[6:0] */
static uint32_t
i_type(uint32_t opcode, unsigned rd, unsigned funct3,
       unsigned rs1, int32_t imm12)
{
	return opcode | (rd << 7) | (funct3 << 12) |
	       (rs1 << 15) | ((uint32_t)(imm12 & 0xFFF) << 20);
}

/* I-type for shift: shamt[5:0] | funct6[5:0] | rs1[4:0] | funct3[2:0] | rd[4:0] | opcode[6:0] */
static uint32_t
i_shift(uint32_t opcode, unsigned rd, unsigned funct3,
        unsigned rs1, unsigned shamt, unsigned funct6)
{
	return opcode | (rd << 7) | (funct3 << 12) |
	       (rs1 << 15) | (shamt << 20) | (funct6 << 26);
}

/* S-type: imm[11:5] | rs2[4:0] | rs1[4:0] | funct3[2:0] | imm[4:0] | opcode[6:0] */
static uint32_t
s_type(uint32_t opcode, unsigned funct3,
       unsigned rs1, unsigned rs2, int32_t imm12)
{
	uint32_t v = opcode | (funct3 << 12) | (rs1 << 15) | (rs2 << 20);
	uint32_t lo = (uint32_t)(imm12 & 0x1F);
	uint32_t hi = (uint32_t)((imm12 >> 5) & 0x7F);
	return v | (lo << 7) | (hi << 25);
}

/* B-type: imm[12|10:5] | rs2[4:0] | rs1[4:0] | funct3[2:0] | imm[4:1|11] | opcode[6:0] */
static uint32_t
b_type(uint32_t opcode, unsigned funct3,
       unsigned rs1, unsigned rs2, int32_t imm)
{
	uint32_t v = opcode | (funct3 << 12) | (rs1 << 15) | (rs2 << 20);
	uint32_t abs = (uint32_t)(imm & 0x1FFF);
	uint32_t lo, hi;
	lo = (abs >> 1) & 0xF;
	hi = (abs >> 5) & 0x3F;
	v |= (lo << 8) | (hi << 25);
	if (abs & 0x1000) v |= (1 << 7) | (1UL << 31);
	if (abs & 0x40) v |= (1UL << 23);
	return v;
}

/* U-type: imm[31:12] | rd[4:0] | opcode[6:0] */
static uint32_t
u_type(uint32_t opcode, unsigned rd, uint32_t imm_31_12)
{
	return opcode | (rd << 7) | (imm_31_12 << 12);
}

/* J-type: imm[20|10:1|11|19:12] | rd[4:0] | opcode[6:0] */
static uint32_t
j_type(uint32_t opcode, unsigned rd, int32_t offset)
{
	uint32_t v = opcode | (rd << 7);
	uint32_t a = (uint32_t)(offset & 0x1FFFFF);
	v |= ((a >> 20) & 1) << 31;    /* bit 20 */
	v |= ((a >> 1) & 0x3FF) << 21; /* bits 10:1 */
	v |= ((a >> 11) & 1) << 20;    /* bit 11 */
	v |= ((a >> 12) & 0xFF) << 12; /* bits 19:12 */
	return v;
}

/* Set a fixup for a relocation */
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

/* ---- Main instruction encoder ---- */

int
riscv64_encode_insn(const struct mt_target *target,
                    const char *mnemonic, const char *operands,
                    struct mt_insn *out)
{
	struct rv_op ops[4];
	int nops = 0;
	size_t mlen;

	(void)target;
	memset(out, 0, sizeof(*out));
	out->fixed = 1;
	out->size = 4;

	parse_operands(operands, ops, &nops);
	mlen = strlen(mnemonic);

/* Macro to set fixup from the first operand's symbol */
#define SYM_FIXUP(reloc, addend) do { \
	if (nops >= 1 && ops[0].kind == 4) { set_fixup(out, 0, 4, (reloc), ops[0].sym, (addend) + ops[0].addend); } \
	else if (nops >= 2 && ops[1].kind == 4) { set_fixup(out, 0, 4, (reloc), ops[1].sym, (addend) + ops[1].addend); } \
} while(0)

	/* === RV64I Base Instructions === */

	/* mv rd, rs1  →  addi rd, rs1, 0 */
	if (strcmp(mnemonic, "mv") == 0 && nops == 2) {
		if (ops[0].kind == 1 && ops[1].kind == 1) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg, 0));
			return 0;
		}
	}

	/* nop  →  addi zero, zero, 0 */
	if (strcmp(mnemonic, "nop") == 0) {
		emit32(out->bytes, i_type(0x13, 0, 0, 0, 0));
		return 0;
	}

	/* ret  →  jalr zero, ra, 0 */
	if (strcmp(mnemonic, "ret") == 0) {
		emit32(out->bytes, i_type(0x67, 0, 0, 1, 0));
		return 0;
	}

	/* ecall */
	if (strcmp(mnemonic, "ecall") == 0) {
		emit32(out->bytes, i_type(0x73, 0, 0, 0, 0));
		return 0;
	}

	/* ebreak */
	if (strcmp(mnemonic, "ebreak") == 0) {
		emit32(out->bytes, i_type(0x73, 0, 0, 0, 1));
		return 0;
	}

	/* fence  →  fence iorw, iorw */
	if (strcmp(mnemonic, "fence") == 0) {
		emit32(out->bytes, i_type(0x0F, 0, 0, 0, 0x0FF));
		return 0;
	}

	/* fence.i */
	if (strcmp(mnemonic, "fence.i") == 0) {
		emit32(out->bytes, 0x0000100F);
		return 0;
	}

	/* lui rd, imm20 */
	if (strcmp(mnemonic, "lui") == 0 && nops >= 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, u_type(0x37, (unsigned)ops[0].reg,
			                          (uint32_t)(ops[1].imm & 0xFFFFF)));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, u_type(0x37, (unsigned)ops[0].reg, 0));
			set_fixup(out, 0, 4, 26 /* R_RISCV_HI20 */,
			           ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* auipc rd, imm20 */
	if (strcmp(mnemonic, "auipc") == 0 && nops >= 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, u_type(0x17, (unsigned)ops[0].reg,
			                          (uint32_t)(ops[1].imm & 0xFFFFF)));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, u_type(0x17, (unsigned)ops[0].reg, 0));
			set_fixup(out, 0, 4, 23 /* R_RISCV_PCREL_HI20 */,
			           ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* addi rd, rs1, imm12 */
	if (strcmp(mnemonic, "addi") == 0 && nops == 3) {
		if (ops[2].kind == 2) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg,
			                          (int32_t)ops[2].imm));
			return 0;
		} else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 24 /* R_RISCV_LO12_I */,
			           ops[2].sym, ops[2].addend);
			return 0;
		}
	}

	/* slti / sltiu */
	if (strcmp(mnemonic, "slti") == 0 && nops == 3) {
		if (ops[2].kind == 2) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 2,
			                          (unsigned)ops[1].reg,
			                          (int32_t)ops[2].imm));
			return 0;
		}
	}
	if (strcmp(mnemonic, "sltiu") == 0 && nops == 3) {
		if (ops[2].kind == 2) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 3,
			                          (unsigned)ops[1].reg,
			                          (int32_t)ops[2].imm));
			return 0;
		}
	}

	/* xori / ori / andi */
	if (strcmp(mnemonic, "xori") == 0 && nops == 3) {
		if (ops[2].kind == 2)
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 4,
			                          (unsigned)ops[1].reg, (int32_t)ops[2].imm));
		else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 4,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 24, ops[2].sym, ops[2].addend);
		}
		return 0;
	}
	if (strcmp(mnemonic, "ori") == 0 && nops == 3) {
		if (ops[2].kind == 2)
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 6,
			                          (unsigned)ops[1].reg, (int32_t)ops[2].imm));
		else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 6,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 24, ops[2].sym, ops[2].addend);
		}
		return 0;
	}
	if (strcmp(mnemonic, "andi") == 0 && nops == 3) {
		if (ops[2].kind == 2)
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 7,
			                          (unsigned)ops[1].reg, (int32_t)ops[2].imm));
		else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 7,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 24, ops[2].sym, ops[2].addend);
		}
		return 0;
	}

	/* slli / srli / srai — shift by immediate (shamt in ops[2]) */
	if ((strcmp(mnemonic, "slli") == 0 || strcmp(mnemonic, "slliw") == 0) && nops == 3) {
		int w = (mnemonic[mlen-1] == 'w') ? 1 : 0;
		unsigned shamt = (unsigned)ops[2].imm & (w ? 0x1F : 0x3F);
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg,
		                          1, (unsigned)ops[1].reg, shamt, w ? 0x00 : 0x00));
		return 0;
	}
	if ((strcmp(mnemonic, "srli") == 0 || strcmp(mnemonic, "srliw") == 0) && nops == 3) {
		int w = (mnemonic[mlen-1] == 'w') ? 1 : 0;
		unsigned shamt = (unsigned)ops[2].imm & (w ? 0x1F : 0x3F);
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg,
		                          5, (unsigned)ops[1].reg, shamt, w ? 0x00 : 0x00));
		return 0;
	}
	if ((strcmp(mnemonic, "srai") == 0 || strcmp(mnemonic, "sraiw") == 0) && nops == 3) {
		int w = (mnemonic[mlen-1] == 'w') ? 1 : 0;
		unsigned shamt = (unsigned)ops[2].imm & (w ? 0x1F : 0x3F);
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg,
		                          5, (unsigned)ops[1].reg, shamt, w ? 0x20 : 0x20));
		return 0;
	}

	/* add/sub (+W) with immediate (GAS-style pseudo):
	 *   add rd, rs, imm  → addi rd, rs, imm
	 *   sub rd, rs, imm  → addi rd, rs, -imm   (RV has no subi)
	 *   addw/subw similarly with addiw. */
	if ((strcmp(mnemonic, "add") == 0 || strcmp(mnemonic, "sub") == 0 ||
	     strcmp(mnemonic, "addw") == 0 || strcmp(mnemonic, "subw") == 0) &&
	    nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    ops[2].kind == 2) {
		int w = (mnemonic[mlen-1] == 'w') ? 1 : 0;
		int32_t imm = (int32_t)ops[2].imm;
		if (strcmp(mnemonic, "sub") == 0 || strcmp(mnemonic, "subw") == 0)
			imm = -imm;
		emit32(out->bytes, i_type(w ? 0x1B : 0x13, (unsigned)ops[0].reg,
		                          0, (unsigned)ops[1].reg, imm));
		return 0;
	}

	/* R-type integer ops (3 registers). Each matched mnemonic returns
	 * directly; an unmatched mnemonic falls through to the next group so
	 * that e.g. addw/subw (W group) are not shadowed by the plain add
	 * block's trailing return -1. */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg, rs1 = (unsigned)ops[1].reg, rs2 = (unsigned)ops[2].reg;
		if (strcmp(mnemonic, "add") == 0)  { emit32(out->bytes, r_type(0x33, rd, 0, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "sub") == 0)  { emit32(out->bytes, r_type(0x33, rd, 0, rs1, rs2, 0x20)); return 0; }
		if (strcmp(mnemonic, "sll") == 0)  { emit32(out->bytes, r_type(0x33, rd, 1, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "slt") == 0)  { emit32(out->bytes, r_type(0x33, rd, 2, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "sltu") == 0) { emit32(out->bytes, r_type(0x33, rd, 3, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "xor") == 0)  { emit32(out->bytes, r_type(0x33, rd, 4, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "srl") == 0)  { emit32(out->bytes, r_type(0x33, rd, 5, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "sra") == 0)  { emit32(out->bytes, r_type(0x33, rd, 5, rs1, rs2, 0x20)); return 0; }
		if (strcmp(mnemonic, "or") == 0)   { emit32(out->bytes, r_type(0x33, rd, 6, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "and") == 0)  { emit32(out->bytes, r_type(0x33, rd, 7, rs1, rs2, 0x00)); return 0; }
	}

	/* R-type with W suffix (RV64): addw, subw, sllw, srlw, sraw */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg, rs1 = (unsigned)ops[1].reg, rs2 = (unsigned)ops[2].reg;
		if (strcmp(mnemonic, "addw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 0, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "subw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 0, rs1, rs2, 0x20)); return 0; }
		if (strcmp(mnemonic, "sllw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 1, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "srlw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 5, rs1, rs2, 0x00)); return 0; }
		if (strcmp(mnemonic, "sraw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 5, rs1, rs2, 0x20)); return 0; }
	}

	/* mul, mulh, mulhu, mulhsu, div, divu, rem, remu */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg, rs1 = (unsigned)ops[1].reg, rs2 = (unsigned)ops[2].reg;
		if (strcmp(mnemonic, "mul") == 0)    { emit32(out->bytes, r_type(0x33, rd, 0, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "mulh") == 0)   { emit32(out->bytes, r_type(0x33, rd, 1, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "mulhu") == 0)  { emit32(out->bytes, r_type(0x33, rd, 3, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "mulhsu") == 0) { emit32(out->bytes, r_type(0x33, rd, 2, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "div") == 0)    { emit32(out->bytes, r_type(0x33, rd, 4, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "divu") == 0)   { emit32(out->bytes, r_type(0x33, rd, 5, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "rem") == 0)    { emit32(out->bytes, r_type(0x33, rd, 6, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "remu") == 0)   { emit32(out->bytes, r_type(0x33, rd, 7, rs1, rs2, 0x01)); return 0; }
	}

	/* W-suffix multiply/divide: mulw, divw, divuw, remw, remuw */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg, rs1 = (unsigned)ops[1].reg, rs2 = (unsigned)ops[2].reg;
		if (strcmp(mnemonic, "mulw") == 0)  { emit32(out->bytes, r_type(0x3B, rd, 0, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "divw") == 0)  { emit32(out->bytes, r_type(0x3B, rd, 4, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "divuw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 5, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "remw") == 0)  { emit32(out->bytes, r_type(0x3B, rd, 6, rs1, rs2, 0x01)); return 0; }
		if (strcmp(mnemonic, "remuw") == 0) { emit32(out->bytes, r_type(0x3B, rd, 7, rs1, rs2, 0x01)); return 0; }
	}

	/* Load/Store (integer + float) — single dispatch on mem operand.
	 * Unmatched mem mnemonics fall through to the atomic handlers. */
	if (nops == 2 && ops[1].kind == 3) {
		int is_load = 0;
		unsigned funct3 = 0;
		uint32_t op = 0;
		/* integer loads (I-type, opcode 0x03) */
		if (strcmp(mnemonic, "lb") == 0)  { is_load = 1; op = 0x03; funct3 = 0; }
		else if (strcmp(mnemonic, "lh") == 0)  { is_load = 1; op = 0x03; funct3 = 1; }
		else if (strcmp(mnemonic, "lw") == 0)  { is_load = 1; op = 0x03; funct3 = 2; }
		else if (strcmp(mnemonic, "ld") == 0)  { is_load = 1; op = 0x03; funct3 = 3; }
		else if (strcmp(mnemonic, "lbu") == 0) { is_load = 1; op = 0x03; funct3 = 4; }
		else if (strcmp(mnemonic, "lhu") == 0) { is_load = 1; op = 0x03; funct3 = 5; }
		else if (strcmp(mnemonic, "lwu") == 0) { is_load = 1; op = 0x03; funct3 = 6; }
		/* integer stores (S-type, opcode 0x23) */
		else if (strcmp(mnemonic, "sb") == 0) { op = 0x23; funct3 = 0; }
		else if (strcmp(mnemonic, "sh") == 0) { op = 0x23; funct3 = 1; }
		else if (strcmp(mnemonic, "sw") == 0) { op = 0x23; funct3 = 2; }
		else if (strcmp(mnemonic, "sd") == 0) { op = 0x23; funct3 = 3; }
		/* float loads (I-type, opcode 0x07) */
		else if (strcmp(mnemonic, "flw") == 0) { is_load = 1; op = 0x07; funct3 = 2; }
		else if (strcmp(mnemonic, "fld") == 0) { is_load = 1; op = 0x07; funct3 = 3; }
		/* float stores (S-type, opcode 0x27) */
		else if (strcmp(mnemonic, "fsw") == 0) { op = 0x27; funct3 = 2; }
		else if (strcmp(mnemonic, "fsd") == 0) { op = 0x27; funct3 = 3; }
		else goto try_atomic;

		unsigned regA = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].mem_reg;
		if (is_load) {
			if (ops[1].sym) {
				emit32(out->bytes, i_type(op, regA, funct3, rs1, 0));
				set_fixup(out, 0, 4, 24 /* R_RISCV_LO12_I */,
				           ops[1].sym, ops[1].addend);
			} else {
				emit32(out->bytes, i_type(op, regA, funct3, rs1,
				                          (int32_t)ops[1].imm));
			}
		} else { /* store (is_load == 0 here) */
			unsigned rs2 = regA;
			if (ops[1].sym) {
				emit32(out->bytes, s_type(op, funct3, rs1, rs2, 0));
				set_fixup(out, 0, 4, 25 /* R_RISCV_LO12_S */,
				           ops[1].sym, ops[1].addend);
			} else {
				emit32(out->bytes, s_type(op, funct3, rs1, rs2,
				                          (int32_t)ops[1].imm));
			}
		}
		return 0;
	}

	/* jal rd, offset  (call: jal ra, symbol; jump: jal zero, symbol) */
	if (strcmp(mnemonic, "jal") == 0 && nops == 2) {
		unsigned rd = (unsigned)ops[0].reg;
		if (ops[1].kind == 2) {
			emit32(out->bytes, j_type(0x6F, rd, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, j_type(0x6F, rd, 0));
			set_fixup(out, 0, 4, 17 /* R_RISCV_JAL */,
			           ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* jalr rd, rs1, imm12  (ret: jalr zero, ra, 0) */
	if (strcmp(mnemonic, "jalr") == 0 && nops == 3) {
		if (ops[2].kind == 2) {
			emit32(out->bytes, i_type(0x67, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg, (int32_t)ops[2].imm));
		} else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x67, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 24, ops[2].sym, ops[2].addend);
		}
		return 0;
	}
	if (strcmp(mnemonic, "jalr") == 0 && nops == 1) {
		/* jalr rs1  →  jalr ra, rs1, 0 */
		emit32(out->bytes, i_type(0x67, 1, 0, (unsigned)ops[0].reg, 0));
		return 0;
	}
	if (strcmp(mnemonic, "jalr") == 0 && nops == 2) {
		/* jalr ra, rs1  →  jalr ra, rs1, 0 */
		emit32(out->bytes, i_type(0x67, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0));
		return 0;
	}

	/* Branch: beq, bne, blt, bge, bltu, bgeu */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1) {
		unsigned rs1 = (unsigned)ops[0].reg, rs2 = (unsigned)ops[1].reg;
		unsigned funct3 = 0;
		if (strcmp(mnemonic, "beq") == 0) funct3 = 0;
		else if (strcmp(mnemonic, "bne") == 0) funct3 = 1;
		else if (strcmp(mnemonic, "blt") == 0) funct3 = 4;
		else if (strcmp(mnemonic, "bge") == 0) funct3 = 5;
		else if (strcmp(mnemonic, "bltu") == 0) funct3 = 6;
		else if (strcmp(mnemonic, "bgeu") == 0) funct3 = 7;
		else return -1;
		if (ops[2].kind == 2) {
			emit32(out->bytes, b_type(0x63, funct3, rs1, rs2,
			                          (int32_t)ops[2].imm));
			return 0;
		} else if (ops[2].kind == 4) {
			emit32(out->bytes, b_type(0x63, funct3, rs1, rs2, 0));
			set_fixup(out, 0, 4, 16 /* R_RISCV_BRANCH */,
			           ops[2].sym, ops[2].addend);
			return 0;
		}
	}

	/* neg rd, rs  →  sub rd, x0, rs */
	if (strcmp(mnemonic, "neg") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x33, (unsigned)ops[0].reg, 0, 0,
		                          (unsigned)ops[1].reg, 0x20));
		return 0;
	}

	/* seqz rd, rs  →  sltiu rd, rs, 1 */
	if (strcmp(mnemonic, "seqz") == 0 && nops == 2) {
		emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 3,
		                          (unsigned)ops[1].reg, 1));
		return 0;
	}

	/* snez rd, rs  →  sltu rd, x0, rs */
	if (strcmp(mnemonic, "snez") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x33, (unsigned)ops[0].reg, 3, 0,
		                          (unsigned)ops[1].reg, 0x00));
		return 0;
	}

	/* sext.b / zext.b / sext.h / zext.h / sext.w / zext.w */
	if (strcmp(mnemonic, "sext.b") == 0 && nops == 2) {
		/* slli rd, rs, 56; srai rd, rd, 56 */
		out->size = 8;
		unsigned rd = (unsigned)ops[0].reg, rs = (unsigned)ops[1].reg;
		emit32(out->bytes, i_shift(0x13, rd, 1, rs, 56, 0));
		emit32(out->bytes + 4, i_shift(0x13, rd, 5, rd, 56, 0x20));
		return 0;
	}
	if (strcmp(mnemonic, "zext.b") == 0 && nops == 2) {
		emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 7,
		                          (unsigned)ops[1].reg, 0xFF));
		return 0;
	}
	if (strcmp(mnemonic, "sext.h") == 0 && nops == 2) {
		unsigned rd = (unsigned)ops[0].reg, rs = (unsigned)ops[1].reg;
		out->size = 8;
		emit32(out->bytes, i_shift(0x13, rd, 1, rs, 48, 0));
		emit32(out->bytes + 4, i_shift(0x13, rd, 5, rd, 48, 0x20));
		return 0;
	}
	if (strcmp(mnemonic, "zext.h") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x33, (unsigned)ops[0].reg, 4, 0, (unsigned)ops[1].reg, 0x04));
		return 0;
	}
	if (strcmp(mnemonic, "sext.w") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x3B, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x00));
		return 0;
	}
	if (strcmp(mnemonic, "zext.w") == 0 && nops == 2) {
		/* addiuw? No — use slli + srli or just: */
		unsigned rd = (unsigned)ops[0].reg, rs = (unsigned)ops[1].reg;
		out->size = 8;
		emit32(out->bytes, i_shift(0x13, rd, 1, rs, 32, 0));
		emit32(out->bytes + 4, i_shift(0x13, rd, 5, rd, 32, 0x20));
		return 0;
	}

	/* Float loads/stores (flw/fld/fsw/fsd) are handled by the combined
	 * load/store dispatch above. */

	/* ---- Pseudoinstructions (mcc / GAS compatibility) ---- */

	/* not rd, rs  →  xori rd, rs, -1 */
	if (strcmp(mnemonic, "not") == 0 && nops == 2 &&
	    ops[0].kind == 1 && ops[1].kind == 1) {
		emit32(out->bytes, i_type(0x13, (unsigned)ops[0].reg, 4,
		                          (unsigned)ops[1].reg, -1));
		return 0;
	}

	/* negw rd, rs  →  subw rd, x0, rs */
	if (strcmp(mnemonic, "negw") == 0 && nops == 2 &&
	    ops[0].kind == 1 && ops[1].kind == 1) {
		emit32(out->bytes, r_type(0x3B, (unsigned)ops[0].reg, 0, 0,
		                          (unsigned)ops[1].reg, 0x20));
		return 0;
	}

	/* j offset  →  jal x0, offset ;  j label  →  jal x0, label (JAL fixup) */
	if (strcmp(mnemonic, "j") == 0 && nops == 1) {
		if (ops[0].kind == 2) {
			emit32(out->bytes, j_type(0x6F, 0, (int32_t)ops[0].imm));
			return 0;
		} else if (ops[0].kind == 4) {
			emit32(out->bytes, j_type(0x6F, 0, 0));
			set_fixup(out, 0, 4, 17 /* R_RISCV_JAL */,
			           ops[0].sym, ops[0].addend);
			return 0;
		}
	}

	/* jr rs  →  jalr x0, rs, 0 */
	if (strcmp(mnemonic, "jr") == 0 && nops == 1 && ops[0].kind == 1) {
		emit32(out->bytes, i_type(0x67, 0, 0, (unsigned)ops[0].reg, 0));
		return 0;
	}

	/* ---- Branch pseudo-instructions (compare with zero) ---- */

	/* beqz rs, offset  →  beq rs, x0, offset */
	if (strcmp(mnemonic, "beqz") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 0, (unsigned)ops[0].reg, 0, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 0, (unsigned)ops[0].reg, 0, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* bnez rs, offset  →  bne rs, x0, offset */
	if (strcmp(mnemonic, "bnez") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 1, (unsigned)ops[0].reg, 0, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 1, (unsigned)ops[0].reg, 0, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* blez rs, offset  →  bge x0, rs, offset (rs <= 0  ↔  0 >= rs) */
	if (strcmp(mnemonic, "blez") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 5, 0, (unsigned)ops[0].reg, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 5, 0, (unsigned)ops[0].reg, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* bgez rs, offset  →  bge rs, x0, offset */
	if (strcmp(mnemonic, "bgez") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 5, (unsigned)ops[0].reg, 0, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 5, (unsigned)ops[0].reg, 0, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* bltz rs, offset  →  blt rs, x0, offset */
	if (strcmp(mnemonic, "bltz") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 4, (unsigned)ops[0].reg, 0, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 4, (unsigned)ops[0].reg, 0, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* bgtz rs, offset  →  blt x0, rs, offset (rs > 0  ↔  0 < rs) */
	if (strcmp(mnemonic, "bgtz") == 0 && nops == 2 && ops[0].kind == 1) {
		if (ops[1].kind == 2) {
			emit32(out->bytes, b_type(0x63, 4, 0, (unsigned)ops[0].reg, (int32_t)ops[1].imm));
			return 0;
		} else if (ops[1].kind == 4) {
			emit32(out->bytes, b_type(0x63, 4, 0, (unsigned)ops[0].reg, 0));
			set_fixup(out, 0, 4, 16, ops[1].sym, ops[1].addend);
			return 0;
		}
	}

	/* tail symbol  →  auipc x6, %pcrel_hi(sym); jalr x0, x6, %pcrel_lo(sym)
	 * Uses t0 (x6) for the jump target, R_RISCV_CALL for the pair. */
	if (strcmp(mnemonic, "tail") == 0 && nops == 1 && ops[0].kind == 4) {
		out->size = 8;
		emit32(out->bytes, u_type(0x17, 6, 0));            /* auipc t0, 0 */
		emit32(out->bytes + 4, i_type(0x67, 0, 0, 6, 0)); /* jalr x0, t0, 0 */
		set_fixup(out, 0, 4, 18 /* R_RISCV_CALL */, ops[0].sym, ops[0].addend);
		return 0;
	}
	if (strcmp(mnemonic, "tail") == 0 && nops == 1 && ops[0].kind == 2) {
		emit32(out->bytes, j_type(0x6F, 0, (int32_t)ops[0].imm));
		return 0;
	}

	/* li rd, imm  — materialize a constant (addi for 12-bit, else lui+addi) */
	if (strcmp(mnemonic, "li") == 0 && nops == 2 && ops[0].kind == 1 &&
	    ops[1].kind == 2) {
		unsigned rd = (unsigned)ops[0].reg;
		int64_t imm = ops[1].imm;
		if (imm >= -2048 && imm <= 2047) {
			emit32(out->bytes, i_type(0x13, rd, 0, 0, (int32_t)imm));
		} else {
			uint64_t u = (uint64_t)imm;
			uint32_t hi20 = (uint32_t)(((u + 0x800) >> 12) & 0xFFFFF);
			uint32_t lo12 = (uint32_t)(u & 0xFFF);
			out->size = 8;
			emit32(out->bytes, u_type(0x37, rd, hi20));
			emit32(out->bytes + 4,
			       i_type(0x13, rd, 0, rd, (int32_t)(int16_t)lo12));
		}
		return 0;
	}

	/* call sym  →  auipc ra, %pcrel_hi(sym); jalr ra, ra, %lo(sym)
	 * Emitted as an 8-byte auipc+jalr pair patched by R_RISCV_CALL (18). */
	if (strcmp(mnemonic, "call") == 0 && nops == 1) {
		if (ops[0].kind == 4) {
			out->size = 8;
			emit32(out->bytes, u_type(0x17, 1, 0));            /* auipc ra, 0 */
			emit32(out->bytes + 4, i_type(0x67, 1, 0, 1, 0)); /* jalr ra, ra, 0 */
			set_fixup(out, 0, 4, 18 /* R_RISCV_CALL */,
			           ops[0].sym, ops[0].addend);
			return 0;
		} else if (ops[0].kind == 2) {
			emit32(out->bytes, j_type(0x6F, 1, (int32_t)ops[0].imm));
			return 0;
		}
	}

try_atomic:
	/* Atomic: lr.w, lr.d, sc.w, sc.d, amoswap.w/d, amoadd.w/d, amoand.w/d, amoor.w/d, amoxor.w/d
	 * Format: lr.w rd, (rs1) — also amoswap.w/d rd, rs2, (rs1) */
	if (nops == 2 && (strcmp(mnemonic, "lr.w") == 0 || strcmp(mnemonic, "lr.d") == 0)) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].mem_reg;
		unsigned funct3 = strcmp(mnemonic, "lr.w") == 0 ? 2 : 3;
		emit32(out->bytes, r_type(0x2F, rd, funct3, rs1, 0, 0x12));
		return 0;
	}
	if (nops == 3 && ops[1].kind == 1 && ops[2].kind == 3) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs2 = (unsigned)ops[1].reg;
		unsigned rs1 = (unsigned)ops[2].mem_reg;
		unsigned funct3 = 2;
		unsigned funct7 = 0x1A;
		if (strcmp(mnemonic, "sc.w") == 0) { funct3 = 2; funct7 = 0x1A; }
		else if (strcmp(mnemonic, "sc.d") == 0) { funct3 = 3; funct7 = 0x1A; }
		else if (strcmp(mnemonic, "amoswap.w") == 0 || strcmp(mnemonic, "amoswap.d") == 0) {
			funct3 = strcmp(mnemonic, "amoswap.d") == 0 ? 3 : 2;
			funct7 = 0x0A;
		}
		else return -1;
		emit32(out->bytes, r_type(0x2F, rd, funct3, rs1, rs2, funct7));
		return 0;
	}

	/* fmv.x.w rd, fs1 / fmv.x.d rd, fs1 — FS[fs1] → X[rd] */
	if (strcmp(mnemonic, "fmv.x.w") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x78));
		return 0;
	}
	if (strcmp(mnemonic, "fmv.x.d") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x79));
		return 0;
	}

	/* fmv.w.x rd, rs1 / fmv.d.x rd, rs1 — X[rs1] → FS[rd] */
	if (strcmp(mnemonic, "fmv.w.x") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[1].reg, 0,
		                          (unsigned)ops[0].reg, 0, 0x78));
		return 0;
	}
	if (strcmp(mnemonic, "fmv.d.x") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[1].reg, 0,
		                          (unsigned)ops[0].reg, 0, 0x79));
		return 0;
	}

	out->size = 0;
	out->ok = 0;
	return -1;
}
