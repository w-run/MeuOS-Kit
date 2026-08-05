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
		/* Require at least one digit: a bare "x" or "f" is a symbol
		 * name, not the x0/f0 register.  strtol("") returns 0 with a
		 * valid-looking end pointer, which would otherwise make e.g.
		 * "call f" mis-parse the label as register f0. */
		char *end;
		long n;
		if (!name[1])
			return -1;
		n = strtol(name + 1, &end, 10);
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

/* Strip surrounding double quotes from a symbol name in place.  mcc
 * quotes local labels containing dots (e.g. ".Lfp1"), but the symbol
 * table entry (defined by the label line) is unquoted, so the fixup
 * must reference the bare name. */
static void
strip_sym_quotes(char **sym)
{
	const char *s = *sym;
	size_t n;
	if (!s)
		return;
	n = strlen(s);
	if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
		memmove((char *)s, s + 1, n - 2);
		((char *)s)[n - 2] = '\0';
	}
}

/* Split a trailing "+N" / "-N" numeric offset from a symbol name in place,
 * returning the symbol part (NUL-terminated) and storing the signed offset
 * in *off.  Quoted symbol names ("...") and offsetless symbols are left
 * untouched (offset 0).  mcc emits "sym+8" for e.g. ga[2] element accesses. */
static char *
split_sym_offset(char *s, int64_t *off)
{
	char *p;

	*off = 0;
	if (!s || !*s || s[0] == '"')
		return s;
	for (p = s; *p; p++) {
		if ((*p == '+' || *p == '-') && p[1] >= '0' && p[1] <= '9') {
			*off = strtoll(p + 1, NULL, 10);
			if (*p == '-')
				*off = -*off;
			*p = '\0';
			break;
		}
	}
	return s;
}

/* Strip quotes (if any) and then split a trailing numeric offset off the
 * symbol name.  Must be called after strip_sym_quotes' quote check on the
 * raw token (quoted names are never split). */
static void
sym_strip_split(char **sym, int64_t *off)
{
	char *ss = *sym;
	int quoted = (ss[0] == '"');
	strip_sym_quotes(sym);
	if (!quoted)
		*sym = split_sym_offset((char *)*sym, off);
}

/* ---- Operand parsing ---- */

struct rv_op {
	int kind;         /* 0=invalid, 1=reg, 2=imm, 3=mem, 4=symbol */
	int reg;          /* register number */
	int64_t imm;      /* immediate value */
	const char *sym;  /* symbol name for fixups */
	int64_t addend;
	int mem_reg;      /* base register for mem operand */
	const char *raw;  /* original token text (for reg/symbol ambiguity) */
	char mod[16];     /* GAS relocation modifier: %hi/%lo/%tprel_hi/... */
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
	while (tok && *nops < 5) {
		while (*tok == ' ') tok++;
		if (!*tok) { tok = strtok(NULL, ","); continue; }

		ops[*nops].kind = 1; /* reg */
		ops[*nops].reg = -1;
		ops[*nops].imm = 0;
		ops[*nops].sym = NULL;
		ops[*nops].addend = 0;
		ops[*nops].mem_reg = -1;
		ops[*nops].raw = tok;
		ops[*nops].mod[0] = '\0';

		/* GAS modifier form: %hi(sym) / %lo(sym). Must be checked
		 * before the memory branch because it also contains '('. */
		if (tok[0] == '%') {
			char *paren = strchr(tok, '(');
			char *endp = strchr(tok, ')');
			if (paren && endp && endp > paren) {
				size_t modlen = (size_t)(paren - (tok + 1));
				if (modlen >= sizeof(ops[*nops].mod))
					modlen = sizeof(ops[*nops].mod) - 1;
				memcpy(ops[*nops].mod, tok + 1, modlen);
				ops[*nops].mod[modlen] = '\0';
				*endp = '\0';
				ops[*nops].kind = 4; /* symbol */
				ops[*nops].sym = paren + 1;
				sym_strip_split((char **)&ops[*nops].sym,
				                &ops[*nops].addend);
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
					long nv = strtol(tok, &endp, 0);
					if (endp != tok) {
						/* Numeric offset: "123(...)" — strtol stops
						 * at the '(' */
						ops[*nops].imm = nv;
					} else {
						/* Symbolic offset "sym" / "sym+N" */
						ops[*nops].sym = tok;
						sym_strip_split((char **)&ops[*nops].sym,
						                &ops[*nops].addend);
					}
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
			sym_strip_split((char **)&ops[*nops].sym,
			                &ops[*nops].addend);
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
				sym_strip_split((char **)&ops[*nops].sym,
				                &ops[*nops].addend);
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

/* Set a second fixup for two-instruction pseudo-ops (e.g. la) */
static void
set_fixup2(struct mt_insn *out, size_t offset, unsigned width,
           unsigned reloc_type, const char *sym, int64_t addend)
{
	out->fixup2_present = 1;
	out->fixup2_offset = offset;
	out->fixup2_width = width;
	out->reloc_type2 = reloc_type;
	out->fixup2_symbol = sym;
	out->fixup2_addend = addend;
}

/* ---- Main instruction encoder ---- */

int
riscv64_encode_insn(const struct mt_target *target,
                    const char *mnemonic, const char *operands,
                    struct mt_insn *out)
{
	struct rv_op ops[5];
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
			set_fixup(out, 0, 4,
			          strcmp(ops[1].mod, "tprel_hi") == 0
			              ? 41 /* R_RISCV_TPREL_HI20 */
			              : 26 /* R_RISCV_HI20 */,
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
			set_fixup(out, 0, 4,
			          strcmp(ops[2].mod, "tprel_lo") == 0
			              ? 39 /* R_RISCV_TPREL_LO12_I */
			              : 27 /* R_RISCV_LO12_I */,
			          ops[2].sym, ops[2].addend);
			return 0;
		}
	}

	/* addiw rd, rs1, imm12 — RV64 add-immediate word (opcode 0x1B, funct3=0) */
	if (strcmp(mnemonic, "addiw") == 0 && nops == 3) {
		if (ops[2].kind == 2) {
			emit32(out->bytes, i_type(0x1B, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg,
			                          (int32_t)ops[2].imm));
			return 0;
		} else if (ops[2].kind == 4) {
			emit32(out->bytes, i_type(0x1B, (unsigned)ops[0].reg, 0,
			                          (unsigned)ops[1].reg, 0));
			set_fixup(out, 0, 4, 27 /* R_RISCV_LO12_I */,
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
			set_fixup(out, 0, 4, 27, ops[2].sym, ops[2].addend);
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
			set_fixup(out, 0, 4, 27, ops[2].sym, ops[2].addend);
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
			set_fixup(out, 0, 4, 27, ops[2].sym, ops[2].addend);
		}
		return 0;
	}

	/* slli / srli / srai — RV32I shift by immediate (opcode 0x13, shamt[5:0]) */
	if (strcmp(mnemonic, "slli") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg, 1,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x3F, 0x00));
		return 0;
	}
	if (strcmp(mnemonic, "srli") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg, 5,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x3F, 0x00));
		return 0;
	}
	if (strcmp(mnemonic, "srai") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x13, (unsigned)ops[0].reg, 5,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x3F, 0x20));
		return 0;
	}

	/* slliw / srliw / sraiw — RV64 shift by immediate (opcode 0x1B, shamt[4:0]) */
	if (strcmp(mnemonic, "slliw") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x1B, (unsigned)ops[0].reg, 1,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x1F, 0x00));
		return 0;
	}
	if (strcmp(mnemonic, "srliw") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x1B, (unsigned)ops[0].reg, 5,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x1F, 0x00));
		return 0;
	}
	if (strcmp(mnemonic, "sraiw") == 0 && nops == 3) {
		emit32(out->bytes, i_shift(0x1B, (unsigned)ops[0].reg, 5,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].imm & 0x1F, 0x20));
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

	/* TLS LE sequence: add rd, rd, tp, %tprel_add(sym)
	 * GAS form has 4 operands: rd, rs1, tp, %tprel_add(sym).  The
	 * R_RISCV_TPREL_ADD relocation contributes nothing in a static
	 * link (the address is already tp-relative via the preceding
	 * %tprel_hi), so emit a plain add rd, rs1, rs2 and record the
	 * relocation for the linker to ignore. */
	if (strcmp(mnemonic, "add") == 0 && nops == 4 &&
	    ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1 &&
	    ops[3].kind == 4 && strcmp(ops[3].mod, "tprel_add") == 0) {
		emit32(out->bytes, r_type(0x33, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg,
		                          (unsigned)ops[2].reg, 0x00));
		set_fixup(out, 0, 4, 38 /* R_RISCV_TPREL_ADD */,
		          ops[3].sym, ops[3].addend);
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

	/* xor / or / and with immediate (mcc may emit xor rd, rs, imm instead of xori) */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 2) {
		unsigned rd = (unsigned)ops[0].reg, rs1 = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "xor") == 0) {
			emit32(out->bytes, i_type(0x13, rd, 4, rs1, (int32_t)ops[2].imm));
			return 0;
		}
		if (strcmp(mnemonic, "or") == 0) {
			emit32(out->bytes, i_type(0x13, rd, 6, rs1, (int32_t)ops[2].imm));
			return 0;
		}
		if (strcmp(mnemonic, "and") == 0) {
			emit32(out->bytes, i_type(0x13, rd, 7, rs1, (int32_t)ops[2].imm));
			return 0;
		}
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

	/* Load/Store with a symbol address and an explicit temporary
	 * register: `ld rd, symbol, tmp` / `fld rd, symbol, tmp`
	 * (mcc's address form for stores and float loads).  Expanded
	 * with absolute addressing (lui + load) — the same R_RISCV_HI20/
	 * R_RISCV_LO12 pair mcc's `lui/addi` address loads use — because
	 * the R_RISCV_PCREL_LO12 reloc in this linker has no auipc label
	 * to anchor the low 12 bits to (P is not subtracted). */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 4 && ops[2].kind == 1) {
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
		else goto try_fpsym;
		{
			unsigned regA = (unsigned)ops[0].reg; /* rd (load) / rs2 (store) */
			unsigned tmp = (unsigned)ops[2].reg;  /* address register */
			out->size = 8;
			emit32(out->bytes, u_type(0x37, tmp, 0)); /* lui tmp, 0 */
			if (is_load)
				emit32(out->bytes + 4, i_type(op, regA, funct3, tmp, 0));
			else
				emit32(out->bytes + 4, s_type(op, funct3, tmp, regA, 0));
			set_fixup(out, 0, 4, 26 /* R_RISCV_HI20 */,
			           ops[1].sym, ops[1].addend);
			set_fixup2(out, 4, 4, is_load ? 27 /* R_RISCV_LO12_I */
			                                : 28 /* R_RISCV_LO12_S */,
			           ops[1].sym, ops[1].addend);
			return 0;
		}
	}

try_fpsym:

	/* Integer load from a bare symbol address with no base register:
	 *   ld/lw/lb/lbu/lh/lhu/lwu rd, symbol
	 * mcc's fixmem leaves SGlo addresses as direct symbol references,
	 * producing this two-operand form.  Expanded with absolute
	 * addressing (lui + load) — the same R_RISCV_HI20/R_RISCV_LO12_I
	 * pair the la pseudo uses — with rd itself as the address base,
	 * since rd is the load target.  Stores from a bare symbol are not
	 * handled here (mcc emits an explicit t6 operand for those). */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 4) {
		unsigned funct3 = 0;
		uint32_t op = 0;
		if (strcmp(mnemonic, "lb") == 0)  { op = 0x03; funct3 = 0; }
		else if (strcmp(mnemonic, "lh") == 0)  { op = 0x03; funct3 = 1; }
		else if (strcmp(mnemonic, "lw") == 0)  { op = 0x03; funct3 = 2; }
		else if (strcmp(mnemonic, "ld") == 0)  { op = 0x03; funct3 = 3; }
		else if (strcmp(mnemonic, "lbu") == 0) { op = 0x03; funct3 = 4; }
		else if (strcmp(mnemonic, "lhu") == 0) { op = 0x03; funct3 = 5; }
		else if (strcmp(mnemonic, "lwu") == 0) { op = 0x03; funct3 = 6; }
		else goto try_loadmem;
		{
			unsigned rd = (unsigned)ops[0].reg;
			out->size = 8;
			emit32(out->bytes, u_type(0x37, rd, 0));                /* lui rd, 0 */
			emit32(out->bytes + 4, i_type(op, rd, funct3, rd, 0));  /* ld rd, 0(rd) */
			set_fixup(out, 0, 4, 26 /* R_RISCV_HI20 */,
			           ops[1].sym, ops[1].addend);
			set_fixup2(out, 4, 4, 27 /* R_RISCV_LO12_I */,
			           ops[1].sym, ops[1].addend);
			return 0;
		}
	}

try_loadmem:
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
				set_fixup(out, 0, 4, 27 /* R_RISCV_LO12_I */,
				           ops[1].sym, ops[1].addend);
			} else {
				emit32(out->bytes, i_type(op, regA, funct3, rs1,
				                          (int32_t)ops[1].imm));
			}
		} else { /* store (is_load == 0 here) */
			unsigned rs2 = regA;
			if (ops[1].sym) {
				emit32(out->bytes, s_type(op, funct3, rs1, rs2, 0));
				set_fixup(out, 0, 4, 28 /* R_RISCV_LO12_S */,
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
			set_fixup(out, 0, 4, 27, ops[2].sym, ops[2].addend);
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

	/* ---- Floating-Point arithmetic (R-type, opcode 0x53) ---- */
	/* Dispatch on .s or .d suffix.  The float-load/store handlers
	 * (flw/fld/fsw/fsd) have already returned above, so .s/.d suffix
	 * here unambiguously selects single/double. */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].reg;
		unsigned rs2 = (unsigned)ops[2].reg;
		unsigned fmt; /* 0 = .s, 1 = .d */
		if (strcmp(mnemonic, "fadd.s") == 0 || strcmp(mnemonic, "fsub.s") == 0 ||
		    strcmp(mnemonic, "fmul.s") == 0 || strcmp(mnemonic, "fdiv.s") == 0 ||
		    strcmp(mnemonic, "fsgnj.s") == 0 || strcmp(mnemonic, "fsgnjn.s") == 0 ||
		    strcmp(mnemonic, "fsgnjx.s") == 0 ||
		    strcmp(mnemonic, "fmin.s") == 0 || strcmp(mnemonic, "fmax.s") == 0 ||
		    strcmp(mnemonic, "feq.s") == 0 || strcmp(mnemonic, "flt.s") == 0 ||
		    strcmp(mnemonic, "fle.s") == 0) {
			fmt = 0;
		} else if (strcmp(mnemonic, "fadd.d") == 0 || strcmp(mnemonic, "fsub.d") == 0 ||
		           strcmp(mnemonic, "fmul.d") == 0 || strcmp(mnemonic, "fdiv.d") == 0 ||
		           strcmp(mnemonic, "fsgnj.d") == 0 || strcmp(mnemonic, "fsgnjn.d") == 0 ||
		           strcmp(mnemonic, "fsgnjx.d") == 0 ||
		           strcmp(mnemonic, "fmin.d") == 0 || strcmp(mnemonic, "fmax.d") == 0 ||
		           strcmp(mnemonic, "feq.d") == 0 || strcmp(mnemonic, "flt.d") == 0 ||
		           strcmp(mnemonic, "fle.d") == 0) {
			fmt = 1;
		} else goto fp_cmp_pseudo;

		if (strcmp(mnemonic, "fadd.s") == 0 || strcmp(mnemonic, "fadd.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (0 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fsub.s") == 0 || strcmp(mnemonic, "fsub.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (1 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fmul.s") == 0 || strcmp(mnemonic, "fmul.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (2 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fdiv.s") == 0 || strcmp(mnemonic, "fdiv.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (3 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fsgnj.s") == 0 || strcmp(mnemonic, "fsgnj.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (4 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fsgnjn.s") == 0 || strcmp(mnemonic, "fsgnjn.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 1, rs1, rs2, (4 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fsgnjx.s") == 0 || strcmp(mnemonic, "fsgnjx.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 2, rs1, rs2, (4 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fmin.s") == 0 || strcmp(mnemonic, "fmin.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, (5 << 2) | fmt)); return 0; }
		if (strcmp(mnemonic, "fmax.s") == 0 || strcmp(mnemonic, "fmax.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 1, rs1, rs2, (5 << 2) | fmt)); return 0; }

		/* FP compare: rd = X register, rs1/rs2 = FP registers */
		{
			unsigned funct7 = (0x14 << 2) | fmt; /* funct5 = 10100 */
			if (strcmp(mnemonic, "feq.s") == 0 || strcmp(mnemonic, "feq.d") == 0)
				{ emit32(out->bytes, r_type(0x53, rd, 2, rs1, rs2, funct7)); return 0; }
			if (strcmp(mnemonic, "flt.s") == 0 || strcmp(mnemonic, "flt.d") == 0)
				{ emit32(out->bytes, r_type(0x53, rd, 1, rs1, rs2, funct7)); return 0; }
			if (strcmp(mnemonic, "fle.s") == 0 || strcmp(mnemonic, "fle.d") == 0)
				{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs2, funct7)); return 0; }
		}
	}

	/* FP compare pseudo-instructions (swap operands vs GAS naming):
	 *   fgt.s/fgt.d rd, a, b  →  flt.s/d rd, b, a
	 *   fge.s/fge.d rd, a, b  →  fle.s/d rd, b, a */
fp_cmp_pseudo:
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned a = (unsigned)ops[1].reg; /* GAS: first operand */
		unsigned b = (unsigned)ops[2].reg; /* GAS: second operand */
		unsigned fmt;
		if (strcmp(mnemonic, "fgt.s") == 0 || strcmp(mnemonic, "fge.s") == 0) fmt = 0;
		else if (strcmp(mnemonic, "fgt.d") == 0 || strcmp(mnemonic, "fge.d") == 0) fmt = 1;
		else goto fp_unary;
		unsigned funct7 = (0x14 << 2) | fmt; /* funct5 = 10100 */
		if (strcmp(mnemonic, "fgt.s") == 0 || strcmp(mnemonic, "fgt.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 1, b, a, funct7)); return 0; } /* flt rd, b, a */
		if (strcmp(mnemonic, "fge.s") == 0 || strcmp(mnemonic, "fge.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, b, a, funct7)); return 0; } /* fle rd, b, a */
	}

	/* fsqrt.s/d rd, rs1  — 2-operand FP unary */
fp_unary:
	/* fmv.s/d rd, rs1  →  fsgnj.s/d rd, rs1, rs1 (copy FP register) */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "fmv.s") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs1, (4 << 2) | 0));
			return 0;
		}
		if (strcmp(mnemonic, "fmv.d") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 0, rs1, rs1, (4 << 2) | 1));
			return 0;
		}
		/* fneg.s/d rd, rs  →  fsgnjn.s/d rd, rs, rs (flip sign bit) */
		if (strcmp(mnemonic, "fneg.s") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 1, rs1, rs1, (4 << 2) | 0));
			return 0;
		}
		if (strcmp(mnemonic, "fneg.d") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 1, rs1, rs1, (4 << 2) | 1));
			return 0;
		}
		/* fabs.s/d rd, rs  →  fsgnjx.s/d rd, rs, rs (clear sign bit) */
		if (strcmp(mnemonic, "fabs.s") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 2, rs1, rs1, (4 << 2) | 0));
			return 0;
		}
		if (strcmp(mnemonic, "fabs.d") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 2, rs1, rs1, (4 << 2) | 1));
			return 0;
		}
	}
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].reg;
		if (strcmp(mnemonic, "fsqrt.s") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 0, rs1, 0, (0xB << 2) | 0));
			return 0;
		}
		if (strcmp(mnemonic, "fsqrt.d") == 0) {
			emit32(out->bytes, r_type(0x53, rd, 0, rs1, 0, (0xB << 2) | 1));
			return 0;
		}
	}

	/* fcvt — convert between float and integer or between float sizes.
	 *
	 *   fcvt.l.d   rd, rs1, rm   — double → signed 64-bit (rd=X, rs1=F)
	 *   fcvt.lu.d  rd, rs1, rm   — double → unsigned 64-bit
	 *   fcvt.w.d   rd, rs1, rm   — double → signed 32-bit
	 *   fcvt.wu.d  rd, rs1, rm   — double → unsigned 32-bit
	 *   fcvt.l.s   rd, rs1, rm   — single → signed 64-bit
	 *   fcvt.lu.s  rd, rs1, rm   — single → unsigned 64-bit
	 *   fcvt.w.s   rd, rs1, rm   — single → signed 32-bit
	 *   fcvt.wu.s  rd, rs1, rm   — single → unsigned 32-bit
	 *
	 *   fcvt.d.l   rd, rs1       — signed 64-bit → double (rd=F, rs1=X)
	 *   fcvt.d.lu  rd, rs1       — unsigned 64-bit → double
	 *   fcvt.d.w   rd, rs1       — signed 32-bit → double
	 *   fcvt.d.wu  rd, rs1       — unsigned 32-bit → double
	 *   fcvt.s.l   rd, rs1       — signed 64-bit → single
	 *   fcvt.s.lu  rd, rs1       — unsigned 64-bit → single
	 *   fcvt.s.w   rd, rs1       — signed 32-bit → single
	 *   fcvt.s.wu  rd, rs1       — unsigned 32-bit → single
	 *
	 *   fcvt.d.s   rd, rs1       — single → double
	 *   fcvt.s.d   rd, rs1       — double → single
	 */
	if (strncmp(mnemonic, "fcvt.", 5) == 0) {
		/* Parse rounding mode from the last operand */
		unsigned rm = 0; /* default: RNE */
		int n_expected = 2;
		/* "to int" conversions accept an optional 3rd rounding-mode operand */
		if (nops == 3 && ops[2].kind == 4 && ops[2].sym) {
			const char *rm_name = ops[2].sym;
			if      (strcmp(rm_name, "rne") == 0) rm = 0;
			else if (strcmp(rm_name, "rtz") == 0) rm = 1;
			else if (strcmp(rm_name, "rdn") == 0) rm = 2;
			else if (strcmp(rm_name, "rup") == 0) rm = 3;
			else if (strcmp(rm_name, "rmm") == 0) rm = 4;
			else if (strcmp(rm_name, "dyn") == 0) rm = 7;
			else { /* treat as symbol, need fixup */ }
			n_expected = 3;
		}

		if (nops < 2 || ops[0].kind != 1 || ops[1].kind != 1)
			goto try_atomic;
		if (n_expected == 3 && nops != 3) goto try_atomic;

		unsigned rd = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].reg;

		/* Encoding helpers for fcvt variants:
		 *   funct7 = (funct5 << 2) | fmt    (fmt=0.s, 1.d)
		 *   rs2 encodes integer format      (0=w, 1=wu, 2=l, 3=lu) */

		/* --- "to int" conversions (FP → X) --- */
		if (strcmp(mnemonic, "fcvt.w.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 0, (0x18 << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.wu.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 1, (0x19 << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.l.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 2, (0x1A << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.lu.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 3, (0x1B << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.w.s") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 0, (0x18 << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.wu.s") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 1, (0x19 << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.l.s") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 2, (0x1A << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.lu.s") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 3, (0x1B << 2) | 0)); return 0; }

		/* --- "from int" conversions (X → FP, rm irrelevant) --- */
		if (strcmp(mnemonic, "fcvt.d.w") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 0, (0x1A << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.d.wu") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 1, (0x1A << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.d.l") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 2, (0x1A << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.d.lu") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 3, (0x1A << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.s.w") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 0, (0x1A << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.s.wu") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 1, (0x1A << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.s.l") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 2, (0x1A << 2) | 0)); return 0; }
		if (strcmp(mnemonic, "fcvt.s.lu") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 3, (0x1A << 2) | 0)); return 0; }

		/* --- between float sizes ---
		 * fcvt.d.s rd, rs1: funct7=0100001, rs2=00000 (single -> double)
		 * fcvt.s.d rd, rs1: funct7=0100000, rs2=00001, funct3=rm
		 *   (double -> single, rounding mode in funct3; mcc emits the
		 *   2-operand form, defaulting to RNE).  Verified against
		 *   riscv64-linux-gnu-as: fcvt.d.s ft0,fs1 = 0x42048053. */
		if (strcmp(mnemonic, "fcvt.d.s") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, 0, rs1, 0, (8 << 2) | 1)); return 0; }
		if (strcmp(mnemonic, "fcvt.s.d") == 0)
			{ emit32(out->bytes, r_type(0x53, rd, rm, rs1, 1, (8 << 2) | 0)); return 0; }
	}

	/* ---- FMADD / FMSUB / FNMADD / FNMSUB (R4-type, opcode 0x43/0x4B/0x4B/0x4F)
	 * Format: fmadd.s/d rd, rs1, rs2, rs3, rm
	 * For now we require a 5-operand form; a simpler pseudo can be added later. */
	if (nops == 5 && ops[0].kind == 1 && ops[1].kind == 1 &&
	    ops[2].kind == 1 && ops[3].kind == 1) {
		unsigned rd  = (unsigned)ops[0].reg;
		unsigned rs1 = (unsigned)ops[1].reg;
		unsigned rs2 = (unsigned)ops[2].reg;
		unsigned rs3 = (unsigned)ops[3].reg;
		unsigned rm = 0;
		if (ops[4].kind == 4 && ops[4].sym) {
			const char *rm_name = ops[4].sym;
			if      (strcmp(rm_name, "rne") == 0) rm = 0;
			else if (strcmp(rm_name, "rtz") == 0) rm = 1;
			else if (strcmp(rm_name, "rdn") == 0) rm = 2;
			else if (strcmp(rm_name, "rup") == 0) rm = 3;
			else if (strcmp(rm_name, "rmm") == 0) rm = 4;
			else if (strcmp(rm_name, "dyn") == 0) rm = 7;
		} else if (ops[4].kind == 2) {
			rm = (unsigned)(ops[4].imm & 7);
		}

		unsigned fmt = 0; /* 0=.s, 1=.d */
		uint32_t op = 0;
		if      (strcmp(mnemonic, "fmadd.s") == 0) { op = 0x43; fmt = 0; }
		else if (strcmp(mnemonic, "fmadd.d") == 0) { op = 0x43; fmt = 1; }
		else if (strcmp(mnemonic, "fmsub.s") == 0) { op = 0x4B; fmt = 0; }
		else if (strcmp(mnemonic, "fmsub.d") == 0) { op = 0x4B; fmt = 1; }
		else if (strcmp(mnemonic, "fnmadd.s") == 0) { op = 0x4F; fmt = 0; }
		else if (strcmp(mnemonic, "fnmadd.d") == 0) { op = 0x4F; fmt = 1; }
		else if (strcmp(mnemonic, "fnmsub.s") == 0) { op = 0x4B; fmt = 0; }
		else if (strcmp(mnemonic, "fnmsub.d") == 0) { op = 0x4B; fmt = 1; }
		else goto try_atomic;

		/* R4: opcode | rd[11:7] | rm[14:12] | fmt[26:25] | rs1[19:15] | rs2[24:20] | rs3[31:27] */
		uint32_t v = op | (rd << 7) | (rm << 12) | (rs1 << 15) |
		             (rs2 << 20) | (rs3 << 27);
		if (fmt) v |= (1 << 25); /* set fmt=01 for double */
		emit32(out->bytes, v);
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
		else goto branch_fallthrough;
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

branch_fallthrough:

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

	/* la rd, symbol  — load address.
	 * Static (non-PIC) absolute addressing: lui + addi with
	 * R_RISCV_HI20 / R_RISCV_LO12_I.  A PC-relative auipc + addi pair
	 * with these relocs would need the LO12 computed relative to the
	 * auipc instruction (P_hi), but the linker applies the LO12 at the
	 * addi's own place, leaving an error of (place & 0xFFF) for any
	 * non-page-aligned instruction address.  Absolute addressing is
	 * correct for the static executables this toolchain produces. */
	if (strcmp(mnemonic, "la") == 0 && nops == 2 &&
	    ops[0].kind == 1 && ops[1].kind == 4) {
		unsigned rd = (unsigned)ops[0].reg;
		out->size = 8;
		emit32(out->bytes, u_type(0x37, rd, 0));              /* lui rd, 0 */
		emit32(out->bytes + 4, i_type(0x13, rd, 0, rd, 0));  /* addi rd, rd, 0 */
		set_fixup(out, 0, 4, 26 /* R_RISCV_HI20 */,
		          ops[1].sym, ops[1].addend);
		set_fixup2(out, 4, 4, 27 /* R_RISCV_LO12_I */,
		           ops[1].sym, ops[1].addend);
		return 0;
	}

	/* li rd, imm  — materialize a constant (addi for 12-bit, else lui+addi,
	 * else a full RV64 sequence for constants that need >32 bits — e.g. the
	 * bit patterns mcc uses to load doubles, which the old lui+addi path
	 * silently truncated to a 32-bit low half). */
	if (strcmp(mnemonic, "li") == 0 && nops == 2 && ops[0].kind == 1 &&
	    ops[1].kind == 2) {
		unsigned rd = (unsigned)ops[0].reg;
		int64_t imm = ops[1].imm;
		if (imm >= -2048 && imm <= 2047) {
			emit32(out->bytes, i_type(0x13, rd, 0, 0, (int32_t)imm));
			return 0;
		}
		if (imm >= -2147483648LL && imm <= 2147483647LL) {
			uint32_t u32 = (uint32_t)imm;
			uint32_t hi20 = (uint32_t)(((u32 + 0x800) >> 12) & 0xFFFFF);
			uint32_t lo12 = u32 & 0xFFF;
			out->size = 8;
			emit32(out->bytes, u_type(0x37, rd, hi20));
			emit32(out->bytes + 4,
			       i_type(0x13, rd, 0, rd, (int32_t)(int16_t)lo12));
			return 0;
		}
		/* Full 64-bit: build from the most-significant 11-bit gridchunk
		 * down via `slli rd,rd,11` + `addi rd,rd,chunk`.  Using 11-bit
		 * unsigned chunks keeps every addi positive within its 12-bit
		 * signed range, so no sign-extension ever corrupts already-built
		 * high bits. */
		{
			uint64_t u = (uint64_t)imm;
			int top = 63;
			while (top > 0 && !((u >> top) & 1)) top--;
			int b = (top / 11) * 11;
			int n = 0;
			unsigned c = (unsigned)((u >> b) & 0x7FF);
			emit32(out->bytes, i_type(0x13, rd, 0, 0, (int32_t)c));
			n = 1;
			for (b -= 11; b >= 0; b -= 11) {
				c = (unsigned)((u >> b) & 0x7FF);
				emit32(out->bytes + (uint32_t)n * 4,
				       i_shift(0x13, rd, 1, rd, 11, 0)); /* slli rd,rd,11 */
				n++;
				emit32(out->bytes + (uint32_t)n * 4,
				       i_type(0x13, rd, 0, rd, (int32_t)c)); /* addi rd,rd,c */
				n++;
			}
			out->size = (uint32_t)n * 4;
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
		} else if (ops[0].kind == 1 && ops[0].raw) {
			/* The callee name may collide with a register name (e.g.
			 * a function literally named `f2` or `x8`).  A `call`
			 * operand is always a symbol, never a register, so treat
			 * the raw token as the symbol. */
			char sym[128];
			const char *p = ops[0].raw;
			size_t i = 0;
			while (*p && i < sizeof(sym) - 1)
				sym[i++] = *p++;
			sym[i] = '\0';
			{
				char *q = sym;
				strip_sym_quotes(&q);
			}
			out->size = 8;
			emit32(out->bytes, u_type(0x17, 1, 0));
			emit32(out->bytes + 4, i_type(0x67, 1, 0, 1, 0));
			set_fixup(out, 0, 4, 18 /* R_RISCV_CALL */, sym, 0);
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
		else 		if (strcmp(mnemonic, "amoswap.w") == 0 || strcmp(mnemonic, "amoswap.d") == 0) {
			funct3 = strcmp(mnemonic, "amoswap.d") == 0 ? 3 : 2;
			funct7 = 0x0A;
		} else if (strcmp(mnemonic, "amoadd.w") == 0 || strcmp(mnemonic, "amoadd.d") == 0) {
			funct3 = strcmp(mnemonic, "amoadd.d") == 0 ? 3 : 2;
			funct7 = 1; /* funct5=00000, rl=1 */
		} else if (strcmp(mnemonic, "amoxor.w") == 0 || strcmp(mnemonic, "amoxor.d") == 0) {
			funct3 = strcmp(mnemonic, "amoxor.d") == 0 ? 3 : 2;
			funct7 = 0x11; /* funct5=00100, rl=1 */
		} else if (strcmp(mnemonic, "amoand.w") == 0 || strcmp(mnemonic, "amoand.d") == 0) {
			funct3 = strcmp(mnemonic, "amoand.d") == 0 ? 3 : 2;
			funct7 = 0x31; /* funct5=01100, rl=1 */
		} else if (strcmp(mnemonic, "amoor.w") == 0 || strcmp(mnemonic, "amoor.d") == 0) {
			funct3 = strcmp(mnemonic, "amoor.d") == 0 ? 3 : 2;
			funct7 = 0x21; /* funct5=01000, rl=1 */
		} else if (strcmp(mnemonic, "amomin.w") == 0 || strcmp(mnemonic, "amomin.d") == 0) {
			funct3 = strcmp(mnemonic, "amomin.d") == 0 ? 3 : 2;
			funct7 = 0x41; /* funct5=10000, rl=1 */
		} else if (strcmp(mnemonic, "amomax.w") == 0 || strcmp(mnemonic, "amomax.d") == 0) {
			funct3 = strcmp(mnemonic, "amomax.d") == 0 ? 3 : 2;
			funct7 = 0x51; /* funct5=10100, rl=1 */
		} else if (strcmp(mnemonic, "amominu.w") == 0 || strcmp(mnemonic, "amominu.d") == 0) {
			funct3 = strcmp(mnemonic, "amominu.d") == 0 ? 3 : 2;
			funct7 = 0x61; /* funct5=11000, rl=1 */
		} else if (strcmp(mnemonic, "amomaxu.w") == 0 || strcmp(mnemonic, "amomaxu.d") == 0) {
			funct3 = strcmp(mnemonic, "amomaxu.d") == 0 ? 3 : 2;
			funct7 = 0x71; /* funct5=11100, rl=1 */
		}
		else return -1;
		emit32(out->bytes, r_type(0x2F, rd, funct3, rs1, rs2, funct7));
		return 0;
	}

	/* fmv.x.w rd, fs1 / fmv.x.d rd, fs1 — FS[fs1] → X[rd] */
	if (strcmp(mnemonic, "fmv.x.w") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x70));
		return 0;
	}
	if (strcmp(mnemonic, "fmv.x.d") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x71));
		return 0;
	}

	/* fmv.w.x rd, rs1 / fmv.d.x rd, rs1 — X[rs1] → FS[rd] */
	if (strcmp(mnemonic, "fmv.w.x") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x78));
		return 0;
	}
	if (strcmp(mnemonic, "fmv.d.x") == 0 && nops == 2) {
		emit32(out->bytes, r_type(0x53, (unsigned)ops[0].reg, 0,
		                          (unsigned)ops[1].reg, 0, 0x79));
		return 0;
	}

	out->size = 0;
	out->ok = 0;
	return -1;
}
