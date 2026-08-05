/* arm_memit.c — ARM (armv7-a) machine-layer assembly emission (MIR-native).
 *
 * Emits the lowered MMOP machine IR as ARM (AAPCS32) assembly.  The
 * output starts with `.syntax unified / .arch armv7ve / .fpu neon` so
 * the VFP and SDIV/UDIV instructions assemble with the GNU assembler.
 *
 * Comparison model: SETCCR lowers to `cmp a, b; mov rD,#0; movCC rD,#1`
 * (the CPSR flags + conditional execution), and the JCC terminator
 * carries the boolean value in src[0], emitted as `cmp a,#0; b.ne`.
 *
 * Value operands are routed through the scratch registers r10/r12 (and
 * r12 for addressing); FP operands through d8/d9 (s16/s17 views for
 * single-precision — d16-d31 have no s-view on ARM).  Immediates are
 * loaded with movw/movt; loads and stores use [base, #off] with the
 * offset materialized in a scratch register when it does not fit the
 * immediate field.
 *
 * Frame: the prologue pushes the callee-saved GPRs + {fp, lr} with
 * `push {...}`, then vpush {d10-d15} (if used), then reserves the slot/
 * alloca frame with `sub sp, sp, #N`, and sets fp = sp + N (the frame
 * top).  Slots are addressed `off(fp)` with off = v->slot - (pushbytes
 * + vpushbytes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "arm_m.h"

static const MTargetM *g_mt;
static MFnM *g_fm;
static int g_alloca_cur;   /* frame-relative cursor for static allocas */
static const char *g_fname;
static int g_slot_base;    /* -(pushbytes + vpushbytes): push area above */

/* ---- width helpers ------------------------------------------------------ */

static int
alloca_size(MMOP op)
{
	switch (op) {
	case MMOP_ALLOCA4:  return 4;
	case MMOP_ALLOCA8:  return 8;
	default:            return 16;
	}
}

static int
alloca_size_ins(const MInsM *in)
{
	if (in->cst && in->cst->kind == MC_INT)
		return (int)in->cst->u.i;
	return alloca_size(in->op);
}

static int
alloca_total(MFnM *fm)
{
	int t = 0;
	for (MBlkM *b = fm->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++) {
			MMOP op = b->ins[i].op;
			if (op == MMOP_ALLOCA4 || op == MMOP_ALLOCA8 ||
			    op == MMOP_ALLOCA16)
				t += alloca_size_ins(&b->ins[i]);
		}
	return t;
}

/* ---- value printing ------------------------------------------------------ */

static void
emit_const(FILE *f, MConst *c)
{
	if (!c) {
		fputs("0", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:  fprintf(f, "%lld", (long long)c->u.i); break;
	case MC_FLT:  fprintf(f, "0x%x", c->u.s); break;
	case MC_ADDR: fprintf(f, "%s", c->u.addr.sym ? c->u.addr.sym : "0"); break;
	default:      fputs("0", f); break;
	}
}

/* Print a register, or a slot as fp-relative memory (only valid as a
 * load/store operand — the caller routes it through a scratch for ALU). */
static void
emit_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("r10", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%s", mreg_name(g_mt, v->reg));
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "%s", mreg_name(g_mt, v->reg));
		else
			fprintf(f, "%d(fp)", v->slot + g_slot_base);
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s", v->sym ? v->sym : "0");
		break;
	default:
		fputs("r10", f);
		break;
	}
}

/* Load a 32-bit immediate into rn with movw/movt (armv7). */
static void
load_imm(FILE *f, const char *rn, int64_t v)
{
	uint32_t u = (uint32_t)v;
	if (u == 0) {
		fprintf(f, "\tmov\t%s, #0\n", rn);
		return;
	}
	if ((u & 0xFFFF0000u) == 0) {
		fprintf(f, "\tmovw\t%s, #0x%x\n", rn, u & 0xFFFF);
		return;
	}
	if ((u & 0xFFFFu) == 0) {
		fprintf(f, "\tmovw\t%s, #0x%x\n", rn, (u >> 16) & 0xFFFF);
		fprintf(f, "\tlsl\t%s, %s, #16\n", rn, rn);
		return;
	}
	fprintf(f, "\tmovw\t%s, #0x%x\n", rn, u & 0xFFFF);
	fprintf(f, "\tmovt\t%s, #0x%x\n", rn, (u >> 16) & 0xFFFF);
}

/* Load a value into a scratch register (handles immediates, slots).
 * rn is "r10"/"r12". */
static void
mv_to_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\tmov\t%s, #0\n", rn);
		return;
	}
	switch (v->kind) {
	case MV_CONST:
		load_imm(f, rn, v->con ? v->con->u.i : 0);
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "\tmov\t%s, %s\n", rn, mreg_name(g_mt, v->reg));
		else {
			/* ldr [fp, #off] */
			int off = v->slot + g_slot_base;
			if (off >= -4095 && off <= 4095)
				fprintf(f, "\tldr\t%s, [fp, #%d]\n", rn, off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tldr\t%s, [fp, r12]\n", rn);
			}
		}
		break;
	case MV_REG:
		fprintf(f, "\tmov\t%s, %s\n", rn, mreg_name(g_mt, v->reg));
		break;
	case MV_GLOBAL: {
		const char *sym = v->sym ? v->sym : "0";
		fprintf(f, "\tmovw\t%s, #:lower16:%s\n", rn, sym);
		fprintf(f, "\tmovt\t%s, #:upper16:%s\n", rn, sym);
		break;
	}
	default:
		fprintf(f, "\tmov\t%s, #0\n", rn);
		break;
	}
}

/* Write a scratch register back into the destination value. */
static void
scratch_to_dst(FILE *f, MVal *d, const char *rn)
{
	if (!d)
		return;
	switch (d->kind) {
	case MV_TEMP:
		if (d->reg >= 0)
			fprintf(f, "\tmov\t%s, %s\n", mreg_name(g_mt, d->reg), rn);
		else {
			int off = d->slot + g_slot_base;
			if (off >= -4095 && off <= 4095)
				fprintf(f, "\tstr\t%s, [fp, #%d]\n", rn, off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tstr\t%s, [fp, r12]\n", rn);
			}
		}
		break;
	case MV_REG:
		fprintf(f, "\tmov\t%s, %s\n", mreg_name(g_mt, d->reg), rn);
		break;
	default:
		break;
	}
}

/* ---- load/store addressing ---------------------------------------------- */

/* Emit the address in MAddr into the scratch register rn (r12). */
static void
emit_addr_to_scratch(FILE *f, MAddr a, const char *rn)
{
	MVal *base = a.base;
	int64_t off = a.off;
	if (base && base->kind == MV_TEMP && base->reg < 0) {
		/* base is a spilled temp whose slot holds a POINTER VALUE —
		 * load it, then add the displacement */
		mv_to_scratch(f, base, rn);
		if (off)
			fprintf(f, "\tadd\t%s, %s, #%lld\n", rn, rn, (long long)off);
		return;
	}
	if (base && base->kind == MV_CONST) {
		load_imm(f, rn, base->con->u.i);
		if (off)
			fprintf(f, "\tadd\t%s, %s, #%lld\n", rn, rn, (long long)off);
		return;
	}
	if (base && base->kind == MV_GLOBAL) {
		const char *sym = base->sym ? base->sym : "0";
		fprintf(f, "\tmovw\t%s, #:lower16:%s\n", rn, sym);
		fprintf(f, "\tmovt\t%s, #:upper16:%s\n", rn, sym);
		if (off)
			fprintf(f, "\tadd\t%s, %s, #%lld\n", rn, rn, (long long)off);
		return;
	}
	if (!base) {
		load_imm(f, rn, off);
		return;
	}
	/* base is a register value (MV_REG or register-resident temp) */
	mv_to_scratch(f, base, rn);
	if (off)
		fprintf(f, "\tadd\t%s, %s, #%lld\n", rn, rn, (long long)off);
}

/* ---- floating point ----------------------------------------------------- */

/* FP scratch registers (excluded from the allocator): d8/d9, whose
 * single-precision views are s16/s17 (d16-d31 have no s-view on ARM). */
#define FR_A "d8"
#define FR_B "d9"
#define FS_A "s16"
#define FS_B "s17"

static const char *arm_cc_suffix(MCC cc);   /* defined below */

/* Load a floating value into a FP scratch register.  is32 selects the
 * s-view (s16/s17) for MT_F32 operands; `dst` is the destination scratch
 * name ("d8"/"d9" or "s16"/"s17") so both operands of a binary op land in
 * distinct registers. */
static void
fload_scratch(FILE *f, MVal *v, bool is32, const char *dst)
{
	if (!v) {
		fprintf(f, "\tfmov\t%s, #0.0\n", is32 ? "s" : "d");
		return;
	}
	switch (v->kind) {
	case MV_CONST: {
		MConst *c = v->con;
		if (c && c->kind == MC_FLT) {
			if (c->type == MT_F32) {
				uint32_t bits;
				memcpy(&bits, &c->u.s, 4);
				load_imm(f, "r10", (int64_t)bits);
				fprintf(f, "\tvmov\t%s, r10\n", dst);
			} else {
				uint64_t bits;
				memcpy(&bits, &c->u.d, 8);
				load_imm(f, "r10", (int64_t)(uint32_t)bits);
				load_imm(f, "r12", (int64_t)(bits >> 32));
				fprintf(f, "\tvmov\t%s, r10, r12\n", dst);
			}
			return;
		}
		fprintf(f, "\tfmov\t%s, #0.0\n", is32 ? "s" : "d");
		return;
	}
	case MV_TEMP:
		if (v->reg >= 0) {
			bool isd = v->reg >= ARM_D0;
			if (isd) {
				int dn = v->reg - ARM_D0;
				if (is32)
					fprintf(f, "\tvmov\t%s, s%d\n", dst, 2 * dn);
				else
					fprintf(f, "\tvmov\t%s, d%d\n", dst, dn);
			} else {
				fprintf(f, "\tmov\t%s, %s\n", "r10",
				        mreg_name(g_mt, v->reg));
			}
		} else {
			int off = v->slot + g_slot_base;
			if (off >= -1020 && off <= 1020)
				fprintf(f, "\tvldr\t%s, [fp, #%d]\n", dst, off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tvldr\t%s, [fp, r12]\n", dst);
			}
		}
		return;
	case MV_REG: {
		bool isd = v->reg >= ARM_D0;
		if (isd) {
			int dn = v->reg - ARM_D0;
			if (is32)
				fprintf(f, "\tvmov\t%s, s%d\n", dst, 2 * dn);
			else
				fprintf(f, "\tvmov\t%s, d%d\n", dst, dn);
		} else {
			fprintf(f, "\tmov\t%s, %s\n", "r10",
			        mreg_name(g_mt, v->reg));
		}
		return;
	}
	default:
		fprintf(f, "\tfmov\t%s, #0.0\n", is32 ? "s" : "d");
		return;
	}
}

/* Store a FP scratch register into the destination value. */
static void
fstore_scratch(FILE *f, MVal *d, bool is32)
{
	const char *dn = is32 ? FS_A : FR_A;
	if (!d)
		return;
	switch (d->kind) {
	case MV_TEMP:
		if (d->reg >= 0) {
			bool isd = d->reg >= ARM_D0;
			if (isd) {
				int dn = d->reg - ARM_D0;
				if (is32)
					fprintf(f, "\tvmov\ts%d, %s\n", 2 * dn, FS_A);
				else
					fprintf(f, "\tvmov\td%d, %s\n", dn, FR_A);
			} else {
				fprintf(f, "\tmov\t%s, %s\n",
				        mreg_name(g_mt, d->reg), "r10");
			}
		} else {
			int off = d->slot + g_slot_base;
			const char *rd = is32 ? FS_A : FR_A;
			if (off >= -1020 && off <= 1020)
				fprintf(f, "\tvstr\t%s, [fp, #%d]\n", rd, off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tvstr\t%s, [fp, r12]\n", rd);
			}
		}
		return;
	case MV_REG: {
		bool isd = d->reg >= ARM_D0;
		if (isd) {
			int dn = d->reg - ARM_D0;
			if (is32)
				fprintf(f, "\tvmov\ts%d, %s\n", 2 * dn, FS_A);
			else
				fprintf(f, "\tvmov\td%d, %s\n", dn, FR_A);
		} else {
			fprintf(f, "\tmov\t%s, %s\n",
			        mreg_name(g_mt, d->reg), "r10");
		}
		return;
	}
	default:
		return;
	}
}

/* dst = (a cc b) ? 1 : 0 for floating operands — vcmp + vmrs + movCC. */
static void
emit_setccr_fp(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	bool is32 = (a && a->type == MT_F32);
	const char *dn = is32 ? FS_A : FR_A;
	const char *dm = is32 ? FS_B : FR_B;
	fload_scratch(f, a, is32, is32 ? FS_A : FR_A);
	fload_scratch(f, b, is32, is32 ? FS_B : FR_B);
	fprintf(f, "\tvcmp.%s\t%s, %s\n", is32 ? "f32" : "f64", dn, dm);
	fputs("\tvmrs\tAPSR_nzcv, fpscr\n", f);
	fprintf(f, "\tmov\tr10, #0\n");
	fprintf(f, "\tmov%s\tr10, #1\n", arm_cc_suffix(cc));
	scratch_to_dst(f, in->dst, "r10");
}

/* ---- condition suffix ---------------------------------------------------- */

static const char *
arm_cc_suffix(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return "eq";
	case MCC_NE:  return "ne";
	case MCC_GE:  return "ge";
	case MCC_LT:  return "lt";
	case MCC_GT:  return "gt";
	case MCC_LE:  return "le";
	case MCC_CS:  return "cs";
	case MCC_CC:  return "cc";
	case MCC_HI:  return "hi";
	case MCC_LS:  return "ls";
	default:      return "eq";
	}
}

/* dst = (src[0] cc src[1]) ? 1 : 0  (cmp + conditional mov). */
static void
emit_setccr(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;

	if (a && b && (a->type == MT_I64 || b->type == MT_I64)) {
		/* i64 comparison: compare high 32 bits first; if equal, compare
		 * low 32 bits with unsigned comparison.  Use a global counter
		 * for unique labels across the whole assembly file. */
		static uint32_t g_i64cc_id;
		int sa = a->slot;
		int sb = b->slot;
		uint32_t id = g_i64cc_id++;
		const char *suffix = arm_cc_suffix(cc);

		/* Map signed CC -> unsigned CC for low 32-bit comparison */
		const char *lsfx;
		switch (cc) {
		case MCC_EQ:  lsfx = "eq";  break;
		case MCC_NE:  lsfx = "ne"; break;
		case MCC_GE:  lsfx = "cs"; break;  /* cs = carry set (unsigned >=) */
		case MCC_LT:  lsfx = "cc";  break; /* cc = carry clear (unsigned <) */
		case MCC_GT:  lsfx = "hi";  break; /* hi = unsigned > */
		case MCC_LE:  lsfx = "ls"; break; /* ls = unsigned <= */
		case MCC_CS:  lsfx = "cs"; break;
		case MCC_CC:  lsfx = "cc";  break;
		case MCC_HI:  lsfx = "hi";  break;
		case MCC_LS:  lsfx = "ls"; break;
		default:      lsfx = "eq";  break;
		}

		load_imm(f, "r10", sa + 4 + g_slot_base);
		fprintf(f, "\tldr\tr10, [fp, r10]\n");
		load_imm(f, "r12", sb + 4 + g_slot_base);
		fprintf(f, "\tldr\tr12, [fp, r12]\n");
		fputs("\tcmp\tr10, r12\n", f);

		if (cc == MCC_EQ) {
			fprintf(f, "\tbne\t.Li64ne%u\n", id);
			load_imm(f, "r10", sa + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			load_imm(f, "r12", sb + g_slot_base);
			fprintf(f, "\tldr\tr12, [fp, r12]\n");
			fputs("\tcmp\tr10, r12\n", f);
			fputs("\tmoveq\tr10, #1\n", f);
			fputs("\tmovne\tr10, #0\n", f);
			fprintf(f, "\tb\t.Li64d%u\n.Li64ne%u:\n"
			        "\tmov\tr10, #0\n.Li64d%u:\n", id, id, id);
		} else if (cc == MCC_NE) {
			fprintf(f, "\tbne\t.Li64t%u\n", id);
			load_imm(f, "r10", sa + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			load_imm(f, "r12", sb + g_slot_base);
			fprintf(f, "\tldr\tr12, [fp, r12]\n");
			fputs("\tcmp\tr10, r12\n", f);
			fputs("\tmovne\tr10, #1\n", f);
			fputs("\tmoveq\tr10, #0\n", f);
			fprintf(f, "\tb\t.Li64d%u\n.Li64t%u:\n"
			        "\tmov\tr10, #1\n.Li64d%u:\n", id, id, id);
		} else {
			fprintf(f, "\tbne\t.Li64hd%u\n", id);
			load_imm(f, "r10", sa + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			load_imm(f, "r12", sb + g_slot_base);
			fprintf(f, "\tldr\tr12, [fp, r12]\n");
			fputs("\tcmp\tr10, r12\n", f);
			fprintf(f, "\tmov%s\tr10, #1\n", lsfx);
			fprintf(f, "\tmov%s\tr10, #0\n", lsfx[0] == 'e' ? "ne" : "eq");
			fprintf(f, "\tb\t.Li64d%u\n.Li64hd%u:\n", id, id);
			fprintf(f, "\tmov%s\tr10, #1\n", suffix);
			fprintf(f, "\tmov%s\tr10, #0\n", suffix[0] == 'e' ? "ne" : "eq");
			fprintf(f, ".Li64d%u:\n", id);
		}
		scratch_to_dst(f, in->dst, "r10");
		return;
	}

	mv_to_scratch(f, a, "r10");
	mv_to_scratch(f, b, "r12");
	fputs("\tcmp\tr10, r12\n", f);
	fprintf(f, "\tmov\tr10, #0\n");
	fprintf(f, "\tmov%s\tr10, #1\n", arm_cc_suffix(cc));
	scratch_to_dst(f, in->dst, "r10");
}

/* ---- binary op table ----------------------------------------------------- */

static const char *
binop_name(MMOP op)
{
	switch (op) {
	case MMOP_ADD:  return "add";
	case MMOP_SUB:  return "sub";
	case MMOP_MUL:  return "mul";
	case MMOP_AND:  return "and";
	case MMOP_OR:   return "orr";
	case MMOP_XOR:  return "eor";
	case MMOP_SHL:  return "lsl";
	case MMOP_SHR:  return "lsr";
	case MMOP_SAR:  return "asr";
	case MMOP_DIV:  return "sdiv";
	case MMOP_UDIV: return "udiv";
	case MMOP_REM:  return "sdiv";   /* remainder via sdiv+mul+sub */
	case MMOP_UREM: return "udiv";
	default:        return "add";
	}
}

/* ---- main emission ------------------------------------------------------- */

/* Branch operand: return a register name; load slot/const values into a
 * scratch register first (ARM branches only take registers). */
static const char *
branch_src(FILE *f, MVal *v, const char *rn)
{
	if (!v)
		return "r10";
	if (v->kind == MV_REG)
		return mreg_name(g_mt, v->reg);
	if (v->kind == MV_TEMP && v->reg >= 0)
		return mreg_name(g_mt, v->reg);
	mv_to_scratch(f, v, rn);
	return rn;
}

static void
emit_load(FILE *f, MMOP op, MType dt, MAddr a, MVal *d)
{
	if (dt == MT_F32 || dt == MT_F64) {
		/* floating-point load into the FP scratch (s16/d8) */
		bool is32 = dt == MT_F32;
		emit_addr_to_scratch(f, a, "r12");
		if (is32)
			fprintf(f, "\tvldr\t%s, [r12]\n", FS_A);
		else
			fprintf(f, "\tvldr\t%s, [r12]\n", FR_A);
		fstore_scratch(f, d, is32);
		return;
	}
	if (dt == MT_I64) {
		/* i64 load: two 32-bit loads from addr and addr+4.
		 * Destination is slot-resident (kl_in_reg==0). */
		emit_addr_to_scratch(f, a, "r12");
		fprintf(f, "\tldr\tr10, [r12]\n");           /* low 32 */
		scratch_to_dst(f, d, "r10");                  /* store low to slot */
		emit_addr_to_scratch(f, a, "r12");            /* recompute addr */
		fprintf(f, "\tldr\tr10, [r12, #4]\n");        /* high 32 */
		/* store high to slot+4 */
		if (d) {
			int off = d->slot + 4 + g_slot_base;
			if (off >= -4095 && off <= 4095)
				fprintf(f, "\tstr\tr10, [fp, #%d]\n", off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
		}
		return;
	}
	const char *lw = "ldr";
	switch (op) {
	case MMOP_LOAD_S8:  lw = "ldrsb"; break;
	case MMOP_LOAD_S16: lw = "ldrsh"; break;
	case MMOP_LOAD_S32: lw = "ldr";   break;
	case MMOP_LOAD_Z8:  lw = "ldrb";  break;
	case MMOP_LOAD_Z16: lw = "ldrh";  break;
	case MMOP_LOAD_Z32: lw = "ldr";   break;
	default: break;
	}
	emit_addr_to_scratch(f, a, "r12");
	fprintf(f, "\t%s\tr10, [r12]\n", lw);
	scratch_to_dst(f, d, "r10");
}

static void
emit_store(FILE *f, MType dt, MAddr a, MVal *s0)
{
	if (dt == MT_F32 || dt == MT_F64) {
		bool is32 = dt == MT_F32;
		/* load the value first: fload_scratch clobbers r10/r12 when the
		 * value is a float constant (movw/movt bit patterns), so the
		 * store address must be computed afterwards */
		fload_scratch(f, s0, is32, is32 ? FS_A : FR_A);
		emit_addr_to_scratch(f, a, "r12");
		if (is32)
			fprintf(f, "\tvstr\t%s, [r12]\n", FS_A);
		else
			fprintf(f, "\tvstr\t%s, [r12]\n", FR_A);
		return;
	}
	if (dt == MT_I64) {
		/* i64 store: two 32-bit stores to addr and addr+4.
		 * Source is slot-resident (kl_in_reg==0 forces spill). */
		int sslot = s0->slot;
		/* Load low from slot into r10 */
		{
			int off = sslot + g_slot_base;
			if (off >= -4095 && off <= 4095)
				fprintf(f, "\tldr\tr10, [fp, #%d]\n", off);
			else {
				load_imm(f, "r12", off);
				fprintf(f, "\tldr\tr10, [fp, r12]\n");
			}
		}
		emit_addr_to_scratch(f, a, "r12");
		fprintf(f, "\tstr\tr10, [r12]\n");              /* store low */
		/* Load high from slot+4 into r10 */
		{
			int off = sslot + 4 + g_slot_base;
			if (off >= -4095 && off <= 4095)
				fprintf(f, "\tldr\tr10, [fp, #%d]\n", off);
			else {
				fputs("\tpush\t{r12}\n", f);
				load_imm(f, "r12", off);
				fprintf(f, "\tldr\tr10, [fp, r12]\n");
				fputs("\tpop\t{r12}\n", f);
			}
		}
		fprintf(f, "\tstr\tr10, [r12, #4]\n");          /* store high */
		return;
	}
	emit_addr_to_scratch(f, a, "r12");
	mv_to_scratch(f, s0, "r10");
	switch (dt) {
	case MT_I8:  fprintf(f, "\tstrb\tr10, [r12]\n"); break;
	case MT_I16: fprintf(f, "\tstrh\tr10, [r12]\n"); break;
	default:     fprintf(f, "\tstr\tr10, [r12]\n"); break;
	}
}

static void
emit_ins(FILE *f, MInsM *in)
{
	MVal *d = in->dst;
	MVal *s0 = in->src[0];
	MVal *s1 = in->src[1];
	MMOP op = in->op;

	switch (op) {
	case MMOP_MOV:
		if (in->dtype == MT_I64) {
			/* i64 on arm: two 32-bit moves via slot.
			 * Both source and destination are slot-resident
			 * (kl_in_reg==0 forces spill). */
			int sslot = s0->slot;
			int dslot = d ? d->slot : 0;
			/* low 32 bits */
			{
				int off = sslot + g_slot_base;
				if (off >= -4095 && off <= 4095)
					fprintf(f, "\tldr\tr10, [fp, #%d]\n", off);
				else {
					load_imm(f, "r12", off);
					fprintf(f, "\tldr\tr10, [fp, r12]\n");
				}
			}
			if (d) {
				int off = dslot + g_slot_base;
				if (off >= -4095 && off <= 4095)
					fprintf(f, "\tstr\tr10, [fp, #%d]\n", off);
				else {
					load_imm(f, "r12", off);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
			}
			/* high 32 bits */
			{
				int off = sslot + 4 + g_slot_base;
				if (off >= -4095 && off <= 4095)
					fprintf(f, "\tldr\tr10, [fp, #%d]\n", off);
				else {
					load_imm(f, "r12", off);
					fprintf(f, "\tldr\tr10, [fp, r12]\n");
				}
			}
			if (d) {
				int off = dslot + 4 + g_slot_base;
				if (off >= -4095 && off <= 4095)
					fprintf(f, "\tstr\tr10, [fp, #%d]\n", off);
				else {
					load_imm(f, "r12", off);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
			}
			return;
		}
		if (s0 && (s0->type == MT_F32 || s0->type == MT_F64)) {
			bool is32 = in->dtype == MT_F32;
			fload_scratch(f, s0, is32, is32 ? FS_A : FR_A);
			fstore_scratch(f, d, is32);
			return;
		}
		mv_to_scratch(f, s0, "r10");
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_MOVSX:
		if (in->dtype == MT_I64) {
			/* Widen a 32-bit source to 64: lo' = value, hi' = sign fill.
			 * Constant sources materialise both halves; runtime sources are
			 * sign-extended with asr #31 (the same "i64 hi is never written"
			 * gap this fills on i386). */
			if (s0 && s0->kind == MV_CONST && s0->con) {
				int64_t cv = s0->con->u.i;
				load_imm(f, "r10", (int32_t)cv);
				load_imm(f, "r12", d->slot + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
				load_imm(f, "r10", (int32_t)((uint64_t)cv >> 32));
				load_imm(f, "r12", d->slot + 4 + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			} else {
				/* runtime source: lo' = value; hi' = sign fill */
				mv_to_scratch(f, s0, "r10");
				scratch_to_dst(f, d, "r10");         /* lo' stored */
				mv_to_scratch(f, d, "r10");          /* reload lo */
				fprintf(f, "\tasr\tr10, r10, #31\n"); /* hi' = sign fill */
				load_imm(f, "r12", d->slot + 4 + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			return;
		}
		mv_to_scratch(f, s0, "r10");
		switch (in->dtype) {
		case MT_I8:  fputs("\tsxtb\tr10, r10\n", f); break;
		case MT_I16: fputs("\tsxth\tr10, r10\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_MOVZX:
		if (in->dtype == MT_I64) {
			/* Zero-extension to 64: lo' = value, hi' = 0 */
			if (s0 && s0->kind == MV_CONST && s0->con) {
				int64_t cv = s0->con->u.i;
				load_imm(f, "r10", (int32_t)cv);
				load_imm(f, "r12", d->slot + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
				load_imm(f, "r10", 0);
				load_imm(f, "r12", d->slot + 4 + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			} else {
				mv_to_scratch(f, s0, "r10");
				scratch_to_dst(f, d, "r10");
				load_imm(f, "r12", d->slot + 4 + g_slot_base);
				fprintf(f, "\tmov\tr10, #0\n");
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			return;
		}
		mv_to_scratch(f, s0, "r10");
		switch (in->dtype) {
		case MT_I8:  fputs("\tuxtb\tr10, r10\n", f); break;
		case MT_I16: fputs("\tuxth\tr10, r10\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_LEA:
		emit_addr_to_scratch(f, in->addr, "r10");
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR:
	case MMOP_DIV: case MMOP_UDIV: case MMOP_REM: case MMOP_UREM: {
		/* i64 on arm: split into low/high with carry/borrow */
		if (in->dtype == MT_I64) {
			int sslot0 = s0 ? s0->slot : 0;
			int sslot1 = s1 ? s1->slot : 0;
			int dslot = d ? d->slot : 0;
			if (op == MMOP_ADD) {
				/* low: adds; high: adc (add with carry).
				 * adds sets the carry flag; movw/movt (load_imm)
				 * does not clobber it. */
				load_imm(f, "r10", sslot0 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fputs("\tadds\tr10, r10, r12\n", f);
				if (d) {
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				load_imm(f, "r10", sslot0 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fputs("\tadc\tr10, r10, r12\n", f);
				if (d) {
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
			} else if (op == MMOP_SUB) {
				/* low: subs; high: sbc (subtract with borrow) */
				load_imm(f, "r10", sslot0 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fputs("\tsubs\tr10, r10, r12\n", f);
				if (d) {
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				load_imm(f, "r10", sslot0 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fputs("\tsbc\tr10, r10, r12\n", f);
				if (d) {
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
			} else if (op == MMOP_SHL || op == MMOP_SHR || op == MMOP_SAR) {
				/* i64 shift on arm: the 64-bit value lives in two 32-bit
				 * halves (lo @ sslot0, hi @ sslot0+4); a correct shift must
				 * move bits across the halves, unlike the per-half fallback
				 * (which loses the carried bits for shifts >= 32 — e.g.
				 * `1LL << 40` would clear the low half instead of moving the
				 * bit up into the high half).  `s` is the (possibly runtime)
				 * shift count; operands are re-read from their slots so only
				 * the r10/r12 scratch registers are needed, staging through
				 * the destination slots.
				 *
				 *   SHL: lo' = lo<<s (s<32) / 0 (s>=32)
				 *        hi' = (hi<<s)|(lo>>(32-s)) (s<32) / lo<<(s-32) (s<64) / 0
				 *   SHR (logical): hi' = hi>>s (s<32) / 0 (s>=32)
				 *        lo' = (lo>>s)|(hi<<(32-s)) (s<32) / hi>>(s-32) (s<64) / 0
				 *   SAR (arith):   hi' = hi>>s (arith, sign fill)
				 *        lo' as SHR for s<32; for s>=32 lo' = hi>>(s-32), hi' = hi>>31 */
				const char *sh = (op == MMOP_SHL) ? "lsl" :
				                 (op == MMOP_SAR) ? "asr" : "lsr";
				/* is_arith: for SHR-style ops whose high half sign-fills */
				bool is_arith = (op == MMOP_SAR);
				/* unique label suffix per shift instruction (a function may
				 * contain several shifts with the same op) */
				static uint32_t g_i64sh_id;
				uint32_t sid = g_i64sh_id++;
				/* ---- materialize constant operands into their slots ----
				 * An i64 shift with a compile-time constant operand (`1LL<<40`,
				 * `x>>32`) arrives here with s0/s1 as MV_CONST.  Arm stores a
				 * 64-bit value in slot pairs (lo@slot, hi@slot+4), but the
				 * const-to-slot fixup only ever materialises part (the lo half
				 * of the value, and never the larger-than-i32 shift count), so
				 * the slot reads below would see uninitialised stack memory.
				 * Normalise MV_CONST operands to fully stored slots first so
				 * the slot-based shift code is valid for every operand kind. */
				if (s0 && s0->kind == MV_CONST && s0->con) {
					int64_t cv = s0->con->u.i;
					load_imm(f, "r10", (int32_t)cv);
					load_imm(f, "r12", sslot0 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					load_imm(f, "r10", (int32_t)((uint64_t)cv >> 32));
					load_imm(f, "r12", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				if (s1 && s1->kind == MV_CONST && s1->con) {
					load_imm(f, "r10", (int32_t)s1->con->u.i);
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				/* ---- branch: s < 32 vs s >= 32 ---- */
				/* load s -> r12, compare */
				load_imm(f, "r12", sslot1 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fprintf(f, "\tcmp\tr12, #32\n");
				fprintf(f, "\tbge\t.Le64s_ge%u\n", sid);
				/* ---- s < 32 path ---- */
				if (op == MMOP_SHL) {
					/* lo' = lo << s */
					load_imm(f, "r10", sslot0 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\tlsl\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* stage lo>>(32-s) in dst low slot */
					load_imm(f, "r10", sslot0 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\trsb\tr12, r12, #32\n");
					fprintf(f, "\tlsr\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* hi' = (hi<<s) | (staged lo>>(32-s)) */
					load_imm(f, "r10", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\tlsl\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\torr\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* restore lo' (overwritten by the staged value) */
					load_imm(f, "r10", sslot0 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\tlsl\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				} else {
					/* SHR/SAR, s < 32: hi' = hi>>s (arith for SAR) */
					load_imm(f, "r10", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\t%s\tr10, r10, r12\n", sh);
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* stage hi<<(32-s) in dst high slot (arm lsr on the
					 * original hi for the OR contribution) */
					load_imm(f, "r10", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\trsb\tr12, r12, #32\n");
					fprintf(f, "\tlsl\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* lo' = (lo>>s) | (staged hi<<(32-s)) */
					load_imm(f, "r10", sslot0 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\t%s\tr10, r10, r12\n", sh);
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\torr\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* restore hi' (overwritten by staged) */
					load_imm(f, "r10", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\t%s\tr10, r10, r12\n", sh);
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				fprintf(f, "\tb\t.Le64s_done%u\n", sid);
				/* ---- s >= 32 path ---- */
				fprintf(f, ".Le64s_ge%u:\n", sid);
				if (op == MMOP_SHL) {
					/* hi' = lo << (s-32) — read lo BEFORE zeroing the dest
					 * low half, in case dest aliases the source slot */
					load_imm(f, "r10", sslot0 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\tsub\tr12, r12, #32\n");
					fprintf(f, "\tlsl\tr10, r10, r12\n");
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* lo' = 0 */
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tmov\tr10, #0\n");
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				} else {
					/* SHR/SAR, s >= 32: lo' = hi >> (s-32), hi' = 0 (logical)
					 * or the sign fill hi>>31 (arith).  Compute lo' first so
					 * the source hi is read before the (possibly aliased)
					 * destination high half is written. */
					load_imm(f, "r10", sslot0 + 4 + g_slot_base);
					fprintf(f, "\tldr\tr10, [fp, r10]\n");
					load_imm(f, "r12", sslot1 + g_slot_base);
					fprintf(f, "\tldr\tr12, [fp, r12]\n");
					fprintf(f, "\tsub\tr12, r12, #32\n");
					fprintf(f, "\t%s\tr10, r10, r12\n", sh);
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
					/* hi' = 0 (logical) or hi>>31 (arith sign fill) */
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					if (is_arith) {
						load_imm(f, "r10", sslot0 + 4 + g_slot_base);
						fprintf(f, "\tldr\tr10, [fp, r10]\n");
						fprintf(f, "\tasr\tr10, r10, #31\n");
					} else {
						fprintf(f, "\tmov\tr10, #0\n");
					}
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				fprintf(f, ".Le64s_done%u:\n", sid);
				return;
			} else {
				/* fallback: load low, do 32-bit op, store; then
				 * load high, do 32-bit op (no carry), store */
				const char *opn = binop_name(op);
				load_imm(f, "r10", sslot0 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fprintf(f, "\t%s\tr10, r10, r12\n", opn);
				if (d) {
					load_imm(f, "r12", dslot + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
				load_imm(f, "r10", sslot0 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr10, [fp, r10]\n");
				load_imm(f, "r12", sslot1 + 4 + g_slot_base);
				fprintf(f, "\tldr\tr12, [fp, r12]\n");
				fprintf(f, "\t%s\tr10, r10, r12\n", opn);
				if (d) {
					load_imm(f, "r12", dslot + 4 + g_slot_base);
					fprintf(f, "\tstr\tr10, [fp, r12]\n");
				}
			}
			return;
		}
		const char *opn = binop_name(op);
		mv_to_scratch(f, s0, "r10");
		mv_to_scratch(f, s1, "r12");
		if (op == MMOP_REM || op == MMOP_UREM) {
			/* rem = a - (a/b)*b; reload a into r12 after the
			 * quotient clobbers it (only r10/r12 are scratch) */
			fprintf(f, "\t%s\tr10, r10, r12\n", opn);
			fprintf(f, "\tmul\tr10, r10, r12\n");
			mv_to_scratch(f, s0, "r12");
			fprintf(f, "\tsub\tr10, r12, r10\n");
		} else {
			fprintf(f, "\t%s\tr10, r10, r12\n", opn);
		}
		scratch_to_dst(f, d, "r10");
		return;
	}
	case MMOP_NEG:
		if (in->dtype == MT_I64) {
			/* i64 negation: low = 0 - low (rsbs), high = 0 - high - !C (rsc).
			 * rsbs sets the carry flag; movw/movt (load_imm) does not
			 * clobber it. */
			int sslot = s0->slot;
			int dslot = d ? d->slot : 0;
			load_imm(f, "r10", sslot + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			fputs("\trsbs\tr10, r10, #0\n", f);
			if (d) {
				load_imm(f, "r12", dslot + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			load_imm(f, "r10", sslot + 4 + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			fputs("\trsc\tr10, r10, #0\n", f);
			if (d) {
				load_imm(f, "r12", dslot + 4 + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			return;
		}
		mv_to_scratch(f, s0, "r10");
		fputs("\trsb\tr10, r10, #0\n", f);
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_NOT:
		if (in->dtype == MT_I64) {
			/* i64 bitwise NOT: mvn both halves */
			int sslot = s0->slot;
			int dslot = d ? d->slot : 0;
			load_imm(f, "r10", sslot + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			fputs("\tmvn\tr10, r10\n", f);
			if (d) {
				load_imm(f, "r12", dslot + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			load_imm(f, "r10", sslot + 4 + g_slot_base);
			fprintf(f, "\tldr\tr10, [fp, r10]\n");
			fputs("\tmvn\tr10, r10\n", f);
			if (d) {
				load_imm(f, "r12", dslot + 4 + g_slot_base);
				fprintf(f, "\tstr\tr10, [fp, r12]\n");
			}
			return;
		}
		mv_to_scratch(f, s0, "r10");
		fputs("\tmvn\tr10, r10\n", f);
		scratch_to_dst(f, d, "r10");
		return;
	case MMOP_SETCCR:
		if (s0 && (s0->type == MT_F32 || s0->type == MT_F64)) {
			emit_setccr_fp(f, in);
			return;
		}
		emit_setccr(f, in);
		return;
	case MMOP_FADD: case MMOP_FSUB: case MMOP_FMUL:
	case MMOP_FDIV: case MMOP_FNEG: case MMOP_FSQRT: {
		bool is32 = in->dtype == MT_F32;
		const char *w = is32 ? "f32" : "f64";
		const char *dn = is32 ? FS_A : FR_A;
		const char *dm = is32 ? FS_B : FR_B;
		const char *opn;
		switch (op) {
		case MMOP_FADD:  opn = "vadd";  break;
		case MMOP_FSUB:  opn = "vsub";  break;
		case MMOP_FMUL:  opn = "vmul";  break;
		case MMOP_FDIV:  opn = "vdiv";  break;
		case MMOP_FNEG:  opn = "vneg";  break;
		default:         opn = "vsqrt"; break;
		}
		if (op == MMOP_FNEG || op == MMOP_FSQRT) {
			fload_scratch(f, s0, is32, is32 ? FS_A : FR_A);
			fprintf(f, "\t%s.%s\t%s, %s\n", opn, w, dn, dn);
		} else {
			fload_scratch(f, s0, is32, is32 ? FS_A : FR_A);
			fload_scratch(f, s1, is32, is32 ? FS_B : FR_B);
			fprintf(f, "\t%s.%s\t%s, %s, %s\n", opn, w, dn, dn, dm);
		}
		fstore_scratch(f, d, is32);
		return;
	}
	case MMOP_CVTSS2SD:   /* f32 -> f64 */
		fload_scratch(f, s0, true, FS_A);
		fputs("\tvcvt.f64.f32\td8, s16\n", f);
		fstore_scratch(f, d, false);
		return;
	case MMOP_CVTSD2SS:   /* f64 -> f32 */
		fload_scratch(f, s0, false, FR_A);
		fputs("\tvcvt.f32.f64\ts16, d8\n", f);
		fstore_scratch(f, d, true);
		return;
	case MMOP_CVTTSS2SI:  /* f32 -> i32 (trunc) */
	case MMOP_CVTTSD2SI: {
		bool is32 = (op == MMOP_CVTTSS2SI);
		fload_scratch(f, s0, is32, is32 ? FS_A : FR_A);
		if (is32)
			fputs("\tvcvt.s32.f32\ts16, s16\n", f);
		else
			fputs("\tvcvt.s32.f64\ts16, d8\n", f);
		fputs("\tvmov\tr10, s16\n", f);
		scratch_to_dst(f, d, "r10");
		return;
	}
	case MMOP_CVTSI2SS:   /* i32 -> f32 */
	case MMOP_CVTSI2SD: {
		bool is32 = (op == MMOP_CVTSI2SS);
		mv_to_scratch(f, s0, "r10");
		fprintf(f, "\tvmov\ts16, r10\n");
		if (is32)
			fputs("\tvcvt.f32.s32\ts16, s16\n", f);
		else
			fputs("\tvcvt.f64.s32\td8, s16\n", f);
		fstore_scratch(f, d, is32);
		return;
	}
	case MMOP_CVTSI2SS_U: /* unsigned i32 -> f32 */
	case MMOP_CVTSI2SD_U: {
		bool is32 = (op == MMOP_CVTSI2SS_U);
		mv_to_scratch(f, s0, "r10");
		fprintf(f, "\tvmov\ts16, r10\n");
		if (is32)
			fputs("\tvcvt.f32.u32\ts16, s16\n", f);
		else
			fputs("\tvcvt.f64.u32\td8, s16\n", f);
		fstore_scratch(f, d, is32);
		return;
	}
	case MMOP_LOAD:
	case MMOP_LOAD_S8: case MMOP_LOAD_S16: case MMOP_LOAD_S32:
	case MMOP_LOAD_Z8: case MMOP_LOAD_Z16: case MMOP_LOAD_Z32:
		emit_load(f, op, in->dtype, in->addr, d);
		return;
	case MMOP_STORE:
		emit_store(f, in->dtype, in->addr, s0);
		return;
	case MMOP_ALLOCA4: case MMOP_ALLOCA8: case MMOP_ALLOCA16: {
		if (s0 && s0->kind != MV_CONST) {
			mv_to_scratch(f, s0, "r10");
			fputs("\tadd\tr10, r10, #7\n"
			      "\tbic\tr10, r10, #7\n"
			      "\tsub\tsp, sp, r10\n"
			      "\tmov\tr10, sp\n", f);
			scratch_to_dst(f, d, "r10");
			if (g_fm)
				g_fm->dynalloc = true;
			return;
		}
		g_alloca_cur -= alloca_size_ins(in);
		/* g_alloca_cur is already a frame-relative fp offset (it starts
		 * at -(fm->slot + pushbytes + vpushbytes) and the frame reserves
		 * the alloca area below it); adding g_slot_base here pushed the
		 * allocas below sp (frame under-allocation, segfault in
		 * float/loop-heavy functions) */
		load_imm(f, "r10", g_alloca_cur);
		fputs("\tadd\tr10, fp, r10\n", f);
		scratch_to_dst(f, d, "r10");
		return;
	}
	case MMOP_SALLOC: {
		int64_t sz = in->cst ? in->cst->u.i : 0;
		load_imm(f, "r10", sz);
		fputs("\tsub\tsp, sp, r10\n", f);
		return;
	}
	case MMOP_JMP:
		return;   /* handled by the block loop */
	case MMOP_JCC: {
		MVal *a = in->src[0];
		MVal *b = in->src[1];
		/* b == const 0: boolean branch (cmp + bne on a) */
		if (b && b->kind == MV_CONST && b->con->u.i == 0) {
			const char *ra = branch_src(f, a, "r10");
			fprintf(f, "\tcmp\t%s, #0\n", ra);
			fprintf(f, "\tb%s\t.L%s.bb%u\n",
			        in->cc == MCC_NE ? "ne" : "eq",
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			goto jcc_fall;
		}
		if (a && a->kind == MV_CONST && a->con->u.i == 0) {
			const char *rb = branch_src(f, b, "r10");
			fprintf(f, "\tcmp\t%s, #0\n", rb);
			fprintf(f, "\tb%s\t.L%s.bb%u\n",
			        in->cc == MCC_NE ? "eq" : "ne",
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			goto jcc_fall;
		}
		/* generic register compare: cmp + b.cc */
		{
			const char *rb = branch_src(f, b, "r12");
			const char *ra = branch_src(f, a, "r10");
			fprintf(f, "\tcmp\t%s, %s\n", ra, rb);
			fprintf(f, "\tb%s\t.L%s.bb%u\n", arm_cc_suffix(in->cc),
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
		}
	jcc_fall:
		/* blocks are emitted in reverse link order, so the false
		 * target is never the physical next block — branch explicitly */
		if (in->blk->s2)
			fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, in->blk->s2->id);
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL) {
			fprintf(f, "\tbl\t%s\n", s0->sym ? s0->sym : "?");
		} else if (s0) {
			mv_to_scratch(f, s0, "r12");
			fprintf(f, "\tblx\tr12\n");
		}
		if (d && !in->td) {
			if (d->type == MT_F32 || d->type == MT_F64) {
				/* float return arrives in d0 (s0 for float): move it
				 * into the FP scratch, then into the result value */
				bool is32 = d->type == MT_F32;
				if (is32)
					fputs("\tvmov\ts16, s0\n", f);
				else
					fputs("\tvmov\td8, d0\n", f);
				fstore_scratch(f, d, is32);
			} else if (d->type == MT_I64) {
				/* i64 return: r0 (low 32) + r1 (high 32) */
				int dslot = d->slot;
				{
					int off = dslot + g_slot_base;
					if (off >= -4095 && off <= 4095)
						fprintf(f, "\tstr\tr0, [fp, #%d]\n", off);
					else {
						load_imm(f, "r12", off);
						fprintf(f, "\tstr\tr0, [fp, r12]\n");
					}
				}
				{
					int off = dslot + 4 + g_slot_base;
					if (off >= -4095 && off <= 4095)
						fprintf(f, "\tstr\tr1, [fp, #%d]\n", off);
					else {
						load_imm(f, "r12", off);
						fprintf(f, "\tstr\tr1, [fp, r12]\n");
					}
				}
			} else {
				scratch_to_dst(f, d, "r0");
			}
		}
		return;
	}
	case MMOP_BLIT: {
		/* aggregate copy: src[1] -> src[0], cst = size.
		 * Use r10 (dest) and r12 (src) as pointers, r4 as data temp.
		 * r4 is callee-saved, so push/pop around the copy. */
		int64_t sz = in->cst ? in->cst->u.i : 0;
		MVal *dp = s0, *sp = s1;
		/* load dest pointer into r10 */
		if (dp && dp->kind == MV_REG)
			fprintf(f, "\tmov\tr10, %s\n", mreg_name(g_mt, dp->reg));
		else
			mv_to_scratch(f, dp, "r10");
		/* load src pointer into r12 */
		if (sp && sp->kind == MV_REG)
			fprintf(f, "\tmov\tr12, %s\n", mreg_name(g_mt, sp->reg));
		else
			mv_to_scratch(f, sp, "r12");
		/* save r4, do the copy, restore r4 */
		fputs("\tpush\t{r4}\n", f);
		int64_t off = 0;
		while (sz >= 4) {
			fprintf(f, "\tldr\tr4, [r12, #%lld]\n", (long long)off);
			fprintf(f, "\tstr\tr4, [r10, #%lld]\n", (long long)off);
			off += 4;
			sz -= 4;
		}
		if (sz >= 2) {
			fprintf(f, "\tldrh\tr4, [r12, #%lld]\n", (long long)off);
			fprintf(f, "\tstrh\tr4, [r10, #%lld]\n", (long long)off);
			off += 2;
			sz -= 2;
		}
		if (sz >= 1) {
			fprintf(f, "\tldrb\tr4, [r12, #%lld]\n", (long long)off);
			fprintf(f, "\tstrb\tr4, [r10, #%lld]\n", (long long)off);
		}
		fputs("\tpop\t{r4}\n", f);
		return;
	}
	case MMOP_CMP: {
		/* compare two values and set CPSR flags: cmp src[0], src[1] */
		mv_to_scratch(f, s0, "r10");
		mv_to_scratch(f, s1, "r12");
		fputs("\tcmp\tr10, r12\n", f);
		return;
	}
	case MMOP_SETCC: {
		/* dst = flags.cc ? 1 : 0 — conditional set from CPSR flags */
		fputs("\tmov\tr10, #0\n", f);
		fprintf(f, "\tmov%s\tr10, #1\n", arm_cc_suffix(in->cc));
		scratch_to_dst(f, d, "r10");
		return;
	}
	case MMOP_RET:
		return;   /* handled by the block loop */
	case MMOP_NOP:
	case MMOP_VASTART:
	case MMOP_VAARG:
		return;
	default:
		fprintf(f, "\t# unsupported MMOP %d\n", (int)op);
		return;
	}
}

static int csave_idx(const MTargetM *mt, int r);

static void
emit_block(FILE *f, MBlkM *b)
{
	fprintf(f, ".L%s.bb%u:\n", g_fname, b->id);
	for (uint32_t i = 0; i < b->nins; i++)
		emit_ins(f, &b->ins[i]);

	switch (b->term.op) {
	case MMOP_JMP:
		fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, b->s1 ? b->s1->id : 0);
		break;
	case MMOP_JCC: {
		MInsM t = b->term;
		t.blk = b;
		emit_ins(f, &t);
		break;
	}
	case MMOP_RET: {
		MInsM t = b->term;
		t.blk = b;
		/* return value move: r0 (int) / d0 (float) */
		if (t.src[0] && (t.src[0]->type == MT_F32 || t.src[0]->type == MT_F64)) {
			/* move the return value into d0 (s0 for float) */
			MVal *v = t.src[0];
			if (v->kind == MV_REG || (v->kind == MV_TEMP && v->reg >= 0)) {
				if (v->reg >= ARM_D0) {
					int dn = v->reg - ARM_D0;
					if (v->type == MT_F32)
						fprintf(f, "\tvmov\ts0, s%d\n", 2 * dn);
					else
						fprintf(f, "\tvmov\td0, d%d\n", dn);
				} else {
					fprintf(f, "\tmov\tr0, %s\n",
					        mreg_name(g_mt, v->reg));
				}
			} else {
				fload_scratch(f, v, v->type == MT_F32,
			       v->type == MT_F32 ? FS_A : FR_A);
				if (v->type == MT_F32)
					fputs("\tvmov\ts0, s16\n", f);
				else
					fputs("\tvmov\td0, d8\n", f);
			}
		} else if (t.src[0]) {
			mv_to_scratch(f, t.src[0], "r0");
		}
		/* epilogue: restore the frame and pop saved registers */
		fputs("\tadd\tsp, fp, #0\n", f);
		if (g_fm && ((g_fm->regsused >> ARM_D10) & 1))
			fputs("\tvpop\t{d10-d15}\n", f);
		fprintf(f, "\tpop\t{%s}\n",
		        ((g_fm->regsused >> ARM_R4) & 1) ?
		        "r4, r5, r6, r7, r8, r9, fp, pc" : "fp, pc");
		break;
	}
	default:
		break;
	}
}

static int
csave_idx(const MTargetM *mt, int r)
{
	int idx = 0;
	for (int i = 0; mt->rclob && mt->rclob[i] >= 0; i++) {
		if (mt->rclob[i] == r)
			return idx;
		idx++;
	}
	return -1;
}

void
mfnm_emit_arm(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	g_fm = fm;
	g_fname = fm->name ? fm->name : "?";

	/* frame base: the push {..} area (8 or 32 bytes) plus the vpush
	 * {d10-d15} area (48 bytes if any d10-d15 is used) sits above the
	 * slot/alloca frame */
	int pushbytes = 8;
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++) {
		int r = fm->mt->rclob[i];
		if (r < ARM_D0 && ((fm->regsused >> r) & 1)) {
			pushbytes = 32;
			break;
		}
	}
	int vpushbytes = ((fm->regsused >> ARM_D10) & 1) ? 48 : 0;
	g_slot_base = -(pushbytes + vpushbytes);

	fm->dynalloc = false;
	for (MBlkM *b = fm->link; !fm->dynalloc && b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++) {
			MMOP op = b->ins[i].op;
			if ((op == MMOP_ALLOCA4 || op == MMOP_ALLOCA8 ||
			     op == MMOP_ALLOCA16) &&
			    b->ins[i].src[0] && b->ins[i].src[0]->kind != MV_CONST) {
				fm->dynalloc = true;
				break;
			}
		}

	int framesize = fm->slot + alloca_total(fm) + pushbytes + vpushbytes;
	framesize = (framesize + 7) & ~7;
	g_alloca_cur = -(fm->slot + pushbytes + vpushbytes);

	fprintf(f, ".syntax unified\n");
	fprintf(f, ".arch armv7ve\n");
	fprintf(f, ".fpu neon\n");
	fprintf(f, ".text\n");
	if (fm->name) {
		if (fm->host && fm->host->export)
			fprintf(f, ".globl %s\n", fm->name);
		fprintf(f, "%s:\n", fm->name);
	}
	fprintf(f, "\tpush\t{%s}\n",
	        pushbytes == 32 ?
	        "r4, r5, r6, r7, r8, r9, fp, lr" : "fp, lr");
	if (vpushbytes)
		fprintf(f, "\tvpush\t{d10-d15}\n");
	/* reserve the frame (large sizes via a scratch register) */
	if (framesize <= 4095)
		fprintf(f, "\tsub\tsp, sp, #%d\n", framesize);
	else {
		load_imm(f, "r12", framesize);
		fprintf(f, "\tsub\tsp, sp, r12\n");
	}
	if (framesize <= 4095)
		fprintf(f, "\tadd\tfp, sp, #%d\n", framesize);
	else {
		load_imm(f, "r12", framesize);
		fprintf(f, "\tadd\tfp, sp, r12\n");
	}

	/* mfnm_addblk prepends, so fm->link is in REVERSE block order and
	 * the entry block sits at the tail.  Branch to the real entry block
	 * after the prologue. */
	if (fm->start)
		fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, fm->start->id);

	for (MBlkM *b = fm->link; b; b = b->link)
		emit_block(f, b);
}
