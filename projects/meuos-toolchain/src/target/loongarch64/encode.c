/* encode.c — LoongArch 64-bit instruction encoder (LA64 base + atomic + FP).

 * All instructions are fixed 4 bytes.  Supports the instruction patterns
 * emitted by mcc's loongarch64 backend and runtime .S files.
 *
 * Syntax:  mnemonic rd, rj, rk    or   mnemonic rd, rj, imm12
 * Registers: $zero, $ra, $tp, $sp, $a0–$a7, $t0–$t8, $s0–$s9
 *            $f0–$f31 (also by name $fa0–$fa7, $ft0–$ft15, $fs0–$fs15) */

#include "mt/target.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Register table ---- */

struct la_reg {
	const char *name;
	int num;      /* 0–31 */
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

/* $fcc0 - $fcc7 */
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
	/* bare $rN / $fN already handled by the named tables */
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

struct la_op {
	int kind;     /* 0=invalid, 1=reg, 2=imm, 3=mem, 4=symbol */
	int reg;
	int64_t imm;
	const char *sym;
	int64_t addend;
	int mem_reg;
};



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

		/* Check for immediate */
		if (tok[0] == '$' && (tok[1] >= '0' && tok[1] <= '9')) {
			/* It's a numeric register name, handled below */
		} else if (tok[0] == '-' || (tok[0] >= '0' && tok[0] <= '9')) {
			char *end;
			long nv = strtol(tok, &end, 0);
			if (end != tok && *end == '\0') {
				ops[*nops].kind = 2;
				ops[*nops].imm = nv;
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
			/* Symbol reference */
			ops[*nops].kind = 4;
			ops[*nops].sym = tok;
			(*nops)++; tok = strtok(NULL, ","); continue;
		}

		/* Check for $fccN (condition flag) — special */
		if (strncmp(tok, "$fcc", 4) == 0) {
			int fcc = parse_fcc(tok);
			if (fcc >= 0) {
				ops[*nops].kind = 1;
				ops[*nops].reg = fcc;
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
		}

		/* Memory: offset(rj) or (rj) */
		{
			const char *paren = strchr(tok, '(');
			if (paren) {
				char regname[32];
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
				if (reglen >= sizeof(regname)) reglen = sizeof(regname)-1;
				memcpy(regname, start, reglen);
				regname[reglen] = '\0';
				ops[*nops].mem_reg = parse_reg(regname);
				(*nops)++; tok = strtok(NULL, ","); continue;
			}
		}

		/* Register */
		{
			int r = parse_reg(tok);
			if (r >= 0) {
				ops[*nops].kind = 1;
				ops[*nops].reg = r;
			} else {
				/* Unknown token — treat as symbol */
				ops[*nops].kind = 4;
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

/* LoongArch fixed-point 3R format: rd = rj op rk
 *   bits = op | rd | (rj<<5) | (rk<<10)   */
static uint32_t
r3(uint32_t op_10_31, unsigned rd, unsigned rj, unsigned rk)
{
	return op_10_31 | rd | (rj << 5) | (rk << 10);
}

/* LoongArch 2RI12 format: rd = rj op imm12
 *   bits = op | rd | (rj<<5) | (imm12<<10)   */
static uint32_t
ri12(uint32_t op_22_31, unsigned rd, unsigned rj, int32_t imm12)
{
	return op_22_31 | rd | (rj << 5) | ((uint32_t)(imm12 & 0xFFF) << 10);
}

/* Set fixup fields */
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

	parse_operands(operands, ops, &nops);

	/* Helper: ops[i] is immediate or symbol */
#define IMVAL(i) (ops[i].kind == 4 ? 0 : ops[i].imm)
#define OP_SYM(i) (ops[i].kind == 4 ? ops[i].sym : NULL)
#define OP_ADD(i) (ops[i].kind == 4 ? ops[i].addend : 0)
#define NEED_FIXUP(i) (ops[i].kind == 4)

	unsigned rd, rj, rk;

	/* === 3-address R-type === */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].reg;
		if (strcmp(mnemonic, "add.w") == 0)      emit32(out->bytes, r3(0x20, rd, rj, rk));
		else if (strcmp(mnemonic, "add.d") == 0)  emit32(out->bytes, r3(0x28, rd, rj, rk));
		else if (strcmp(mnemonic, "sub.w") == 0)  emit32(out->bytes, r3(0x22, rd, rj, rk));
		else if (strcmp(mnemonic, "sub.d") == 0)  emit32(out->bytes, r3(0x2A, rd, rj, rk));
		else if (strcmp(mnemonic, "and") == 0)    emit32(out->bytes, r3(0x04, rd, rj, rk));
		else if (strcmp(mnemonic, "or") == 0)     emit32(out->bytes, r3(0x05, rd, rj, rk));
		else if (strcmp(mnemonic, "xor") == 0)    emit32(out->bytes, r3(0x06, rd, rj, rk));
		else if (strcmp(mnemonic, "sll.w") == 0)  emit32(out->bytes, r3(0x0E, rd, rj, rk));
		else if (strcmp(mnemonic, "srl.w") == 0)  emit32(out->bytes, r3(0x0F, rd, rj, rk));
		else if (strcmp(mnemonic, "sra.w") == 0)  emit32(out->bytes, r3(0x10, rd, rj, rk));
		else if (strcmp(mnemonic, "sll.d") == 0)  emit32(out->bytes, r3(0x2E, rd, rj, rk));
		else if (strcmp(mnemonic, "srl.d") == 0)  emit32(out->bytes, r3(0x2F, rd, rj, rk));
		else if (strcmp(mnemonic, "sra.d") == 0)  emit32(out->bytes, r3(0x30, rd, rj, rk));
		else if (strcmp(mnemonic, "slt") == 0)    emit32(out->bytes, r3(0x08, rd, rj, rk));
		else if (strcmp(mnemonic, "sltu") == 0)   emit32(out->bytes, r3(0x09, rd, rj, rk));
		else if (strcmp(mnemonic, "mul.w") == 0)  emit32(out->bytes, r3(0x24, rd, rj, rk));
		else if (strcmp(mnemonic, "mul.d") == 0)  emit32(out->bytes, r3(0x2C, rd, rj, rk));
		else if (strcmp(mnemonic, "div.w") == 0)  emit32(out->bytes, r3(0x25, rd, rj, rk));
		else if (strcmp(mnemonic, "div.d") == 0)  emit32(out->bytes, r3(0x2D, rd, rj, rk));
		else if (strcmp(mnemonic, "mod.w") == 0)  emit32(out->bytes, r3(0x26, rd, rj, rk));
		else if (strcmp(mnemonic, "mod.d") == 0)  emit32(out->bytes, r3(0x2E, rd, rj, rk));
		else if (strcmp(mnemonic, "div.wu") == 0) emit32(out->bytes, r3(0x25, rd, rj, rk) | 0x400);  /* rk|0x40 sets unsigned */
		else if (strcmp(mnemonic, "div.du") == 0) emit32(out->bytes, r3(0x2D, rd, rj, rk) | 0x400);
		else if (strcmp(mnemonic, "mod.wu") == 0) emit32(out->bytes, r3(0x26, rd, rj, rk) | 0x400);
		else if (strcmp(mnemonic, "mod.du") == 0) emit32(out->bytes, r3(0x2E, rd, rj, rk) | 0x400);
		else return -1;
		return 0;
	}

	/* === ADD immediate (2RI12) === */
	if ((nops == 3 || nops == 2) && ops[0].kind == 1) {
		rd = (unsigned)ops[0].reg;
		if (nops >= 3) rj = (unsigned)ops[1].reg; else rj = rd;
		int is_mem = (nops >= 2 && ops[nops-1].kind == 3);
		const char *need_fixup = NEED_FIXUP(nops-1) ? OP_SYM(nops-1) : NULL;
		int64_t addend_val = OP_ADD(nops-1);
		int32_t imm_val = (int32_t)IMVAL(nops-1);

		/* Load: ld.b, ld.h, ld.w, ld.d, ld.bu, ld.hu, ld.wu */
		if (strcmp(mnemonic, "ld.b") == 0 && is_mem) {
		    if (need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x0A0<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x0A0<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"ld.h")==0 && is_mem) { emit32(out->bytes,ri12(0x0A4<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"ld.w")==0 && is_mem) { emit32(out->bytes,ri12(0x0A8<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"ld.d")==0 && is_mem) { emit32(out->bytes,ri12(0x0AA<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"ld.bu")==0 && is_mem) { emit32(out->bytes,ri12(0x0A1<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"ld.hu")==0 && is_mem) { emit32(out->bytes,ri12(0x0A5<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"ld.wu")==0 && is_mem) { emit32(out->bytes,ri12(0x0A9<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		/* Store: st.b, st.h, st.w, st.d */
		else if (strcmp(mnemonic,"st.b")==0 && is_mem) { emit32(out->bytes,ri12(0x0B0<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"st.h")==0 && is_mem) { emit32(out->bytes,ri12(0x0B4<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"st.w")==0 && is_mem) { emit32(out->bytes,ri12(0x0B8<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"st.d")==0 && is_mem) { emit32(out->bytes,ri12(0x0BA<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		/* Float loads/stores */
		else if ((strcmp(mnemonic,"fld.s")==0||strcmp(mnemonic,"fld.w")==0) && is_mem) { emit32(out->bytes,ri12(0x0AC<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if ((strcmp(mnemonic,"fld.d")==0) && is_mem) { emit32(out->bytes,ri12(0x0AD<<22,rd,rj,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if ((strcmp(mnemonic,"fst.s")==0||strcmp(mnemonic,"fst.w")==0) && is_mem) { emit32(out->bytes,ri12(0x0BC<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if ((strcmp(mnemonic,"fst.d")==0) && is_mem) { emit32(out->bytes,ri12(0x0BD<<22,rj,rd,need_fixup?0:imm_val));
		    if(need_fixup) set_fixup(out,0,4,21,need_fixup,addend_val); }
		else if (strcmp(mnemonic,"addi.w")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x0A8<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x0A8<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"addi.d")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x0AA<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x0AA<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"slti")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x200<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x200<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"sltui")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x201<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x201<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"ori")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x204<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x204<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"xori")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x205<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x205<<22, rd, rj, imm_val));
		}
		else if (strcmp(mnemonic,"andi")==0) {
		    if(need_fixup) { set_fixup(out,0,4,21,need_fixup,addend_val); emit32(out->bytes,ri12(0x206<<22,rd,rj,0)); }
		    else emit32(out->bytes, ri12(0x206<<22, rd, rj, imm_val));
		}
		else return -1;
		return 0;
	}

	/* === 2RI14: slli.w/d, srli.w/d, srai.w/d === */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 2) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		int32_t shamt = (int32_t)ops[2].imm;
		if (strcmp(mnemonic,"slli.w")==0) { emit32(out->bytes,0x00 << 22 | rd | (rj<<5) | ((shamt & 0x1F) << 10)); return 0; }
		if (strcmp(mnemonic,"slli.d")==0) { emit32(out->bytes,0x22 << 22 | rd | (rj<<5) | ((shamt & 0x3F) << 10)); return 0; }
		if (strcmp(mnemonic,"srli.w")==0) { emit32(out->bytes,0x01 << 22 | rd | (rj<<5) | ((shamt & 0x1F) << 10)); return 0; }
		if (strcmp(mnemonic,"srli.d")==0) { emit32(out->bytes,0x23 << 22 | rd | (rj<<5) | ((shamt & 0x3F) << 10)); return 0; }
		if (strcmp(mnemonic,"srai.w")==0) { emit32(out->bytes,0x02 << 22 | rd | (rj<<5) | ((shamt & 0x1F) << 10)); return 0; }
		if (strcmp(mnemonic,"srai.d")==0) { emit32(out->bytes,0x24 << 22 | rd | (rj<<5) | ((shamt & 0x3F) << 10)); return 0; }
	}

	/* === 2RI14 shift helpers for variable shifts === */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].reg;
		if (strcmp(mnemonic,"sll.w")==0) { return 0; } /* already handled in 3R above */
		if (strcmp(mnemonic,"sll.d")==0) { return 0; } /* ditto */
	}

	/* === 1RI21: lu12i.w rd, imm20 === */
	if (nops == 2 && ops[0].kind == 1) {
		rd = (unsigned)ops[0].reg;
		if (strcmp(mnemonic,"lu12i.w")==0) {
			if (NEED_FIXUP(1)) {
				emit32(out->bytes, 0x14 << 24 | rd | 0x400000);
				set_fixup(out, 0, 4, 20, OP_SYM(1), OP_ADD(1));
			} else {
				uint32_t imm20 = (uint32_t)(ops[1].imm & 0xFFFFF);
				emit32(out->bytes, 0x14 << 24 | rd | (imm20 << 5));
			}
			return 0;
		}
	}

	/* === Branches: beq, bne, blt, bge, bltu, bgeu === */
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1) {
		rj = (unsigned)ops[0].reg;
		rd = (unsigned)ops[1].reg;
		const char *sym = NEED_FIXUP(2) ? OP_SYM(2) : NULL;
		int32_t off = (int32_t)IMVAL(2);
		uint32_t base = 0;
		if (strcmp(mnemonic,"beq")==0) base = 0x50 << 24;
		else if (strcmp(mnemonic,"bne")==0) base = 0x51 << 24;
		else if (strcmp(mnemonic,"blt")==0) base = 0x56 << 24;
		else if (strcmp(mnemonic,"bge")==0) base = 0x57 << 24;
		else if (strcmp(mnemonic,"bltu")==0) base = 0x58 << 24;
		else if (strcmp(mnemonic,"bgeu")==0) base = 0x59 << 24;
		else return -1;
		if (sym) {
			emit32(out->bytes, base | rd | (rj << 5));
			set_fixup(out, 0, 4, 5, sym, OP_ADD(2));
		} else {
			emit32(out->bytes, base | rd | (rj << 5) | ((uint32_t)(off & 0xFFFF) << 10));
		}
		return 0;
	}

	/* === Unconditional branches: b offset, bl offset === */
	if (nops == 1 && ops[0].kind == 2) {
		int32_t off = (int32_t)ops[0].imm;
		if (strcmp(mnemonic,"b")==0) {
			emit32(out->bytes, 0x50 << 24 | 0 | ((uint32_t)(off & 0xFFFF) << 10));
			return 0;
		}
		if (strcmp(mnemonic,"bl")==0) {
			emit32(out->bytes, 0x54 << 24 | 1 | ((uint32_t)(off & 0xFFFF) << 10));
			return 0;
		}
	}
	if (nops == 1 && ops[0].kind == 4) {
		if (strcmp(mnemonic,"bl")==0) {
			emit32(out->bytes, 0x54 << 24 | 1);
			set_fixup(out, 0, 4, 5, OP_SYM(0), OP_ADD(0));
			return 0;
		}
		if (strcmp(mnemonic,"b")==0) {
			emit32(out->bytes, 0x50 << 24 | 0);
			set_fixup(out, 0, 4, 5, OP_SYM(0), OP_ADD(0));
			return 0;
		}
	}

	/* === jr rd (jump register) === */
	if (strcmp(mnemonic,"jr")==0 && nops == 1 && ops[0].kind == 1) {
		emit32(out->bytes, 0x4C << 24 | 0 | (ops[0].reg << 5));
		return 0;
	}

	/* === jirl rd, rj, offset === */
	if (strcmp(mnemonic,"jirl")==0) {
		rd = (nops >= 1 && ops[0].kind == 1) ? (unsigned)ops[0].reg : 1;
		rj = (nops >= 2 && ops[1].kind == 1) ? (unsigned)ops[1].reg : 0;
		int32_t off = (nops >= 3) ? (int32_t)ops[2].imm : 0;
		if (nops == 2 && ops[0].kind == 1) { rd = 1; rj = (unsigned)ops[0].reg; }
		if (NEED_FIXUP(nops-1)) {
			set_fixup(out, 0, 4, 21, OP_SYM(nops-1), OP_ADD(nops-1));
			off = 0;
		}
		emit32(out->bytes, 0x4C << 24 | rd | (rj << 5) | ((uint32_t)(off & 0xFFFF) << 10));
		return 0;
	}

	/* === ret  →  jirl $zero, $ra, 0 === */
	if (strcmp(mnemonic,"ret")==0) {
		emit32(out->bytes, 0x4C << 24 | 0 | (1 << 5));
		return 0;
	}

	/* === nop  →  andi $zero, $zero, 0 === */
	if (strcmp(mnemonic,"nop")==0) {
		emit32(out->bytes, 0x206 << 22 | 0 | 0);
		return 0;
	}

	/* === syscall === */
	if (strcmp(mnemonic,"syscall")==0) {
		int32_t code = (nops >= 1) ? (int32_t)ops[0].imm : 0;
		emit32(out->bytes, 0x18 << 24 | (code & 0x1F));
		return 0;
	}

	/* === Atomic: ll.w, ll.d, sc.w, sc.d === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 3) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].mem_reg;
		int32_t imm = (int32_t)ops[1].imm;
		if (strcmp(mnemonic,"ll.w")==0) { emit32(out->bytes, 0x0A0 << 24 | rd | (rj<<5) | ((imm&0xFFF)<<10)); return 0; }
		if (strcmp(mnemonic,"ll.d")==0) { emit32(out->bytes, 0x0AA << 24 | rd | (rj<<5) | ((imm&0xFFF)<<10)); return 0; }
	}
	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 3) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].mem_reg;
		int32_t imm = (int32_t)ops[2].imm;
		if (strcmp(mnemonic,"sc.w")==0) { emit32(out->bytes, 0x0A1 << 24 | rd | (rj<<5) | (rk<<10) | ((imm&0xFFF)<<10)); return 0; }
		if (strcmp(mnemonic,"sc.d")==0) { emit32(out->bytes, 0x0AB << 24 | rd | (rj<<5) | (rk<<10) | ((imm&0xFFF)<<10)); return 0; }
	}

	/* === dbar === */
	if (strcmp(mnemonic,"dbar")==0) {
		int32_t hint = (nops >= 1) ? (int32_t)ops[0].imm : 0;
		emit32(out->bytes, 0x0B << 28 | (hint & 0x1F));
		return 0;
	}

	/* === ibcl (instruction barrier) === */
	if (strcmp(mnemonic,"ibcl")==0) {
		emit32(out->bytes, 0x0B << 28 | 0x20);
		return 0;
	}

	/* === Extension: ext.w.b, ext.w.h, bstrpick.d === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (strcmp(mnemonic,"ext.w.b")==0) { emit32(out->bytes, 0x3 << 20 | rd | (rj<<5) | (7<<8) | (0<<10)); return 0; }
		if (strcmp(mnemonic,"ext.w.h")==0) { emit32(out->bytes, 0x3 << 20 | rd | (rj<<5) | (15<<8) | (0<<10)); return 0; }
		if (strcmp(mnemonic,"add.w")==0)   { emit32(out->bytes, r3(0x20, rd, rj, 0)); return 0; }  /* add.w rd, rj, $zero */
		/* bstrpick.d rd, rj, msb, lsb */
		if (strcmp(mnemonic,"bstrpick.d")==0 && nops == 4) {
			int msb = (int)ops[2].imm;
			int lsb = (int)ops[3].imm;
			emit32(out->bytes, 0x34 << 20 | rd | (rj<<5) | (msb<<10) | (lsb<<16));
			return 0;
		}
	}

	/* === Float comparison: fcmp.cond.s/d $fcc0, fj, fk === */
	if (strncmp(mnemonic,"fcmp.",5)==0 && nops == 3) {
		int fcc = ops[0].reg; /* $fcc0 */
		unsigned fj = (unsigned)ops[1].reg;
		unsigned fk = (unsigned)ops[2].reg;
		int is_d = (strstr(mnemonic,".d") != NULL);
		int cond = 0;
		if (strstr(mnemonic,".s")) cond = is_d ? 0 : 16;
		cond = 8; /* default c.eq */
		if (strstr(mnemonic,"ceq")) cond = 8;
		if (strstr(mnemonic,"clt")) cond = 4;
		if (strstr(mnemonic,"cle")) cond = 6;
		if (strstr(mnemonic,"cuge")) cond = 10;
		if (strstr(mnemonic,"cult")) cond = 5;
		if (strstr(mnemonic,"cne")) cond = 9;
		if (strstr(mnemonic,"cor")) cond = 14;
		if (strstr(mnemonic,"cun")) cond = 2;
		uint32_t base = is_d ? 0xD << 24 : 0xC << 24;
		emit32(out->bytes, base | (fcc << 0) | (fj << 5) | (fk << 10) | (cond << 20));
		return 0;
	}

	/* === movcf2gr rd, $fcc0 === */
	if (strcmp(mnemonic,"movcf2gr")==0 && nops >= 2) {
		rd = (unsigned)ops[0].reg;
		int fcc = ops[1].reg & 0x7;
		emit32(out->bytes, 0x3 << 20 | rd | (fcc << 5));
		return 0;
	}

	/* === movgr2fr.w / movgr2fr.d / movfr2gr.s / movfr2gr.d === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		if (strcmp(mnemonic,"movfr2gr.w") == 0) { emit32(out->bytes, 0x3 << 20 | rd | (rj<<5) | (0<<15)); return 0; }
		if (strcmp(mnemonic,"movfr2gr.d") == 0) { emit32(out->bytes, 0x3 << 20 | rd | (rj<<5) | (1<<15)); return 0; }
		if (strcmp(mnemonic,"movgr2fr.w") == 0) { emit32(out->bytes, 0x3 << 20 | rj | (rd<<5) | (0<<15)); return 0; }
		if (strcmp(mnemonic,"movgr2fr.d") == 0) { emit32(out->bytes, 0x3 << 20 | rj | (rd<<5) | (1<<15)); return 0; }
	}

	/* === fneg.s/d, fabs.s/d, fmov.s/d === */
	if (nops == 2 && ops[0].kind == 1 && ops[1].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		int is_d = 0;
		if (strstr(mnemonic,".d")) is_d = 1;
		uint32_t op_base = is_d ? 0xD << 24 : 0xC << 24;
		if (strcmp(mnemonic,"fneg.s")==0||strcmp(mnemonic,"fneg.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (2 << 15)); return 0; }
		if (strcmp(mnemonic,"fabs.s")==0||strcmp(mnemonic,"fabs.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (5 << 15)); return 0; }
	}

	if (nops == 3 && ops[0].kind == 1 && ops[1].kind == 1 && ops[2].kind == 1) {
		rd = (unsigned)ops[0].reg;
		rj = (unsigned)ops[1].reg;
		rk = (unsigned)ops[2].reg;
		int is_d = 0;
		if (strstr(mnemonic,".d")) is_d = 1;
		uint32_t op_base = is_d ? 0xD << 24 : 0xC << 24;
		if (strcmp(mnemonic,"fadd.s")==0||strcmp(mnemonic,"fadd.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (0<<15)); return 0; }
		if (strcmp(mnemonic,"fsub.s")==0||strcmp(mnemonic,"fsub.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (1<<15)); return 0; }
		if (strcmp(mnemonic,"fmul.s")==0||strcmp(mnemonic,"fmul.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (2<<15)); return 0; }
		if (strcmp(mnemonic,"fdiv.s")==0||strcmp(mnemonic,"fdiv.d")==0) { emit32(out->bytes, op_base | rd | (rj<<5) | (rk<<10) | (3<<15)); return 0; }
	}

	out->size = 0;
	out->ok = 0;
	return -1;
}
