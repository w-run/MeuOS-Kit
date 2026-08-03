/* aarch64_memit.c — aarch64 machine-layer assembly emission (MIR-native).
 *
 * Emits the lowered MMOP machine IR as aarch64 (AAPCS64) assembly.
 *
 * Comparison model: SETCCR lowers to `cmp a, b; cset dst, cc` (aarch64
 * flags), and the JCC terminator carries the boolean value in src[0],
 * emitted as cbnz/cbz — same register-comparison scheme as riscv64.
 *
 * Value operands are routed through the scratch registers x9/x10/x11
 * (and x16/x17 for addressing) before use, mirroring the riscv64
 * t0/t1/t2 routing.  Immediates are loaded with movz/movk chains; loads
 * and stores use [base, #off] with the offset materialized in a scratch
 * register when it does not fit the immediate field.
 *
 * Frame: the prologue reserves the whole frame in one `sub sp, sp, #N`
 * and stores fp/lr with `stp x29, x30, [sp, #N-16]`, then sets
 * fp = sp + N (old sp).  Slots are addressed `off(fp)` with
 * off = v->slot - 16 (the 16 bytes of the fp+lr save area).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "aarch64_m.h"

static const MTargetM *g_mt;
static MFnM *g_fm;
static int g_alloca_cur;   /* frame-relative cursor for static allocas */
static const char *g_fname;
static int g_slot_base;    /* -16: fp+lr save area above the slots */

static void emit_tls_addr(FILE *f, const char *sym, bool isext,
                          const char *rn);

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
		fputs("xzr", f);
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
			fprintf(f, "%d(x29)", v->slot + g_slot_base);
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s", v->sym ? v->sym : "0");
		break;
	default:
		fputs("xzr", f);
		break;
	}
}

/* Load a 64-bit immediate into rn with movz/movk chains. */
static void
load_imm(FILE *f, const char *rn, int64_t v)
{
	bool first = true;
	for (int shift = 0; shift < 64; shift += 16) {
		uint16_t chunk = (uint64_t)v >> shift;
		if (first) {
			fprintf(f, "\tmovz\t%s, #0x%x, lsl #%d\n", rn, chunk, shift);
			first = false;
		} else if (chunk) {
			fprintf(f, "\tmovk\t%s, #0x%x, lsl #%d\n", rn, chunk, shift);
		}
	}
	if (first)
		fprintf(f, "\tmov\t%s, xzr\n", rn);
}

/* Load a value into a scratch register (handles immediates, slots).
 * rn is "x9"/"x10"/"x11". */
static void
mv_to_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\tmov\t%s, xzr\n", rn);
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
			/* ldr [x29, #off]; off = v->slot - 16 */
			int off = v->slot + g_slot_base;
			if (off >= -256 && off <= 255)
				fprintf(f, "\tldr\t%s, [x29, #%d]\n", rn, off);
			else {
				load_imm(f, "x16", off);
				fprintf(f, "\tldr\t%s, [x29, x16]\n", rn);
			}
		}
		break;
	case MV_REG:
		fprintf(f, "\tmov\t%s, %s\n", rn, mreg_name(g_mt, v->reg));
		break;
	case MV_GLOBAL: {
		if (v->tls) {
			emit_tls_addr(f, v->sym ? v->sym : "0", v->isext, rn);
			break;
		}
		const char *sym = v->sym ? v->sym : "0";
		fprintf(f, "\tadrp\t%s, %s\n", rn, sym);
		fprintf(f, "\tadd\t%s, %s, :lo12:%s\n", rn, rn, sym);
		break;
	}
	default:
		fprintf(f, "\tmov\t%s, xzr\n", rn);
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
			if (off >= -256 && off <= 255)
				fprintf(f, "\tstr\t%s, [x29, #%d]\n", rn, off);
			else {
				load_imm(f, "x16", off);
				fprintf(f, "\tstr\t%s, [x29, x16]\n", rn);
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

/* Emit the address in MAddr into the scratch register rn (x16). */
static void
emit_addr_to_scratch(FILE *f, MAddr a, const char *rn)
{
	MVal *base = a.base;
	int64_t off = a.off;
	if (base && base->kind == MV_TEMP && base->reg < 0) {
		/* base is a spill slot: address it off(x29) */
		load_imm(f, rn, base->slot + g_slot_base);
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
		if (base->tls) {
			emit_tls_addr(f, sym, base->isext, rn);
		} else {
			fprintf(f, "\tadrp\t%s, %s\n", rn, sym);
			fprintf(f, "\tadd\t%s, %s, :lo12:%s\n", rn, rn, sym);
		}
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

/* Emit the address of a TLS global into the scratch register rn:
 *   - local-exec (internal symbol): tpidr_el0 + tprel_hi/lo;
 *   - initial-exec (external): GOTTPREL pair + tpidr_el0. */
static void
emit_tls_addr(FILE *f, const char *sym, bool isext, const char *rn)
{
	if (isext) {
		fprintf(f, "\tadrp\t%s, :gottprel:%s\n", rn, sym);
		fprintf(f, "\tldr\t%s, [%s, :gottprel_lo12:%s]\n", rn, rn, sym);
		fprintf(f, "\tmrs\tx11, tpidr_el0\n");
		fprintf(f, "\tadd\t%s, %s, x11\n", rn, rn);
		return;
	}
	fprintf(f, "\tmrs\t%s, tpidr_el0\n", rn);
	fprintf(f, "\tadd\t%s, %s, #:tprel_hi12:%s\n", rn, rn, sym);
	fprintf(f, "\tadd\t%s, %s, #:tprel_lo12:%s\n", rn, rn, sym);
}

/* FP scratch registers (excluded from the allocator): v16/v17.  Emitted
 * through their s/d views (s16/d16/s17/d17). */
#define FR_A "16"
#define FR_B "17"

static const char *a64_cc_suffix(MCC cc);   /* defined below */

/* Print a floating value's operand: "sN"/"dN" view of a V register, or a
 * slot as fp-relative memory (for ldr/str).  Returns NULL for regs. */
static const char *
freg_name(MVal *v, bool is32)
{
	static char buf[16];
	int r = v->reg;
	if (v->kind == MV_REG || (v->kind == MV_TEMP && v->reg >= 0)) {
		if (r >= A64MREG_V0)
			snprintf(buf, sizeof buf, "%s%d", is32 ? "s" : "d",
			         r - A64MREG_V0);
		else
			snprintf(buf, sizeof buf, "%s", mreg_name(g_mt, r));
		return buf;
	}
	return NULL;
}

/* Load a floating value into a FP scratch register (constants via their
 * bit pattern in a GPR + fmov, slots via ldr s/d). */
static void
fload_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\tfmov\ts%s, wzr\n", rn);   /* 0.0f */
		return;
	}
	switch (v->kind) {
	case MV_CONST: {
		MConst *c = v->con;
		if (c && c->kind == MC_FLT) {
			if (c->type == MT_F32) {
				load_imm(f, "x9", (int64_t)(uint32_t)c->u.s);
				fprintf(f, "\tfmov\ts%s, w9\n", rn);
			} else {
				uint64_t bits;
				memcpy(&bits, &c->u.d, 8);
				load_imm(f, "x9", (int64_t)bits);
				fprintf(f, "\tfmov\td%s, x9\n", rn);
			}
			return;
		}
		fprintf(f, "\tfmov\ts%s, wzr\n", rn);
		return;
	}
	case MV_TEMP:
		if (v->reg >= 0) {
			bool is32 = v->type == MT_F32;
			fprintf(f, "\tfmov\t%s%s, %s\n",
			        is32 ? "s" : "d", rn, freg_name(v, is32));
		} else {
			int off = v->slot + g_slot_base;
			const char *rw = v->type == MT_F32 ? "s" : "d";
			if (off >= -256 && off <= 255)
				fprintf(f, "\tldr\t%s%s, [x29, #%d]\n", rw, rn, off);
			else {
				load_imm(f, "x16", off);
				fprintf(f, "\tldr\t%s%s, [x29, x16]\n", rw, rn);
			}
		}
		return;
	case MV_REG: {
		bool is32 = v->type == MT_F32;
		fprintf(f, "\tfmov\t%s%s, %s\n",
		        is32 ? "s" : "d", rn, freg_name(v, is32));
		return;
	}
	default:
		fprintf(f, "\tfmov\ts%s, wzr\n", rn);
		return;
	}
}

/* Store a FP scratch register into the destination value. */
static void
fstore_scratch(FILE *f, MVal *d, const char *rn)
{
	if (!d)
		return;
	switch (d->kind) {
	case MV_TEMP:
		if (d->reg >= 0) {
			bool is32 = d->type == MT_F32;
			fprintf(f, "\tfmov\t%s, %s%s\n",
			        freg_name(d, is32), is32 ? "s" : "d", rn);
		} else {
			int off = d->slot + g_slot_base;
			const char *rw = d->type == MT_F32 ? "s" : "d";
			if (off >= -256 && off <= 255)
				fprintf(f, "\tstr\t%s%s, [x29, #%d]\n", rw, rn, off);
			else {
				load_imm(f, "x16", off);
				fprintf(f, "\tstr\t%s%s, [x29, x16]\n", rw, rn);
			}
		}
		return;
	case MV_REG: {
		bool is32 = d->type == MT_F32;
		fprintf(f, "\tfmov\t%s, %s%s\n",
		        freg_name(d, is32), is32 ? "s" : "d", rn);
		return;
	}
	default:
		return;
	}
}

/* dst = (a cc b) ? 1 : 0 for floating operands — fcmp + cset (NZCV). */
static void
emit_setccr_fp(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	const char *w = (a && a->type == MT_F32) ? "s" : "d";
	fload_scratch(f, a, FR_A);
	fload_scratch(f, b, FR_B);
	fprintf(f, "\tfcmp\t%s%s, %s%s\n", w, FR_A, w, FR_B);
	fprintf(f, "\tcset\tw9, %s\n", a64_cc_suffix(cc));
	scratch_to_dst(f, in->dst, "x9");
}

/* ---- condition suffix ---------------------------------------------------- */

static const char *
a64_cc_suffix(MCC cc)
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

/* dst = (src[0] cc src[1]) ? 1 : 0  (cmp + cset). */
static void
emit_setccr(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	bool is32 = a && a->type == MT_I32;
	mv_to_scratch(f, a, "x9");
	mv_to_scratch(f, b, "x10");
	fprintf(f, "\tcmp\t%s9, %s10\n", is32 ? "w" : "x", is32 ? "w" : "x");
	fprintf(f, "\tcset\t%s9, %s\n", is32 ? "w" : "x",
	        a64_cc_suffix(cc));
	scratch_to_dst(f, in->dst, "x9");
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
 * scratch register first (aarch64 branches only take registers). */
static const char *
branch_src(FILE *f, MVal *v, const char *rn)
{
	if (!v)
		return "xzr";
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
	const char *lw = "ldr";
	if (dt == MT_F32 || dt == MT_F64) {
		/* floating-point load into the FP scratch (s16/d16) */
		emit_addr_to_scratch(f, a, "x16");
		fprintf(f, "\tldr\t%s16, [x16]\n", dt == MT_F32 ? "s" : "d");
		fstore_scratch(f, d, FR_A);
		return;
	}
	switch (op) {
	case MMOP_LOAD_S8:  lw = "ldrsb"; break;
	case MMOP_LOAD_S16: lw = "ldrsh"; break;
	case MMOP_LOAD_S32: lw = "ldrsw"; break;
	case MMOP_LOAD_Z8:  lw = "ldrb";  break;
	case MMOP_LOAD_Z16: lw = "ldrh";  break;
	case MMOP_LOAD_Z32: lw = "ldr";   break;   /* w form zero-extends */
	case MMOP_LOAD:     lw = dt == MT_I32 ? "ldr" : "ldr"; break;
	default: break;
	}
	emit_addr_to_scratch(f, a, "x16");
	if (dt == MT_I32 || op == MMOP_LOAD_S32 || op == MMOP_LOAD_Z32)
		fprintf(f, "\t%s\tw9, [x16]\n", lw);
	else
		fprintf(f, "\t%s\tx9, [x16]\n", lw);
	scratch_to_dst(f, d, "x9");
}

static void
emit_store(FILE *f, MType dt, MAddr a, MVal *s0)
{
	const char *sw = dt == MT_I32 ? "str" : "str";
	if (dt == MT_F32 || dt == MT_F64) {
		/* floating-point store from the FP scratch (s16/d16) */
		emit_addr_to_scratch(f, a, "x16");
		fload_scratch(f, s0, FR_A);
		fprintf(f, "\tstr\t%s16, [x16]\n", dt == MT_F32 ? "s" : "d");
		return;
	}
	emit_addr_to_scratch(f, a, "x16");
	mv_to_scratch(f, s0, "x9");
	if (dt == MT_I8)  sw = "strb";
	if (dt == MT_I16) sw = "strh";
	fprintf(f, "\t%s\t%s, [x16]\n", sw,
	        (dt == MT_I32 || dt == MT_I8 || dt == MT_I16) ? "w9" : "x9");
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
		if (s0 && (s0->type == MT_F32 || s0->type == MT_F64)) {
			fload_scratch(f, s0, FR_A);
			fstore_scratch(f, d, FR_A);
			return;
		}
		mv_to_scratch(f, s0, "x9");
		scratch_to_dst(f, d, "x9");
		return;
	case MMOP_MOVSX:
		mv_to_scratch(f, s0, "x9");
		switch (in->dtype) {
		case MT_I8:  fputs("\tsxtb\tx9, w9\n", f); break;
		case MT_I16: fputs("\tsxth\tx9, w9\n", f); break;
		case MT_I32: fputs("\tsxtw\tx9, w9\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "x9");
		return;
	case MMOP_MOVZX:
		mv_to_scratch(f, s0, "x9");
		switch (in->dtype) {
		case MT_I8:  fputs("\tuxtb\tx9, w9\n", f); break;
		case MT_I16: fputs("\tuxth\tx9, w9\n", f); break;
		case MT_I32: fputs("\tmov\tw9, w9\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "x9");
		return;
	case MMOP_LEA:
		emit_addr_to_scratch(f, in->addr, "x9");
		scratch_to_dst(f, d, "x9");
		return;
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR:
	case MMOP_DIV: case MMOP_UDIV: case MMOP_REM: case MMOP_UREM: {
		const char *opn = binop_name(op);
		mv_to_scratch(f, s0, "x9");
		mv_to_scratch(f, s1, "x10");
		if (op == MMOP_REM || op == MMOP_UREM) {
			/* remainder: rem = a - (a/b)*b */
			fprintf(f, "\t%s\tx11, x9, x10\n", opn);
			fprintf(f, "\tmul\tx11, x11, x10\n");
			fprintf(f, "\tsub\tx9, x9, x11\n");
		} else {
			fprintf(f, "\t%s\tx9, x9, x10\n", opn);
		}
		scratch_to_dst(f, d, "x9");
		return;
	}
	case MMOP_NEG:
		mv_to_scratch(f, s0, "x9");
		fputs("\tneg\tx9, x9\n", f);
		scratch_to_dst(f, d, "x9");
		return;
	case MMOP_NOT:
		mv_to_scratch(f, s0, "x9");
		fputs("\tmvn\tx9, x9\n", f);
		scratch_to_dst(f, d, "x9");
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
		const char *w = (in->dtype == MT_F32) ? "s" : "d";
		const char *opn;
		switch (op) {
		case MMOP_FADD:  opn = "fadd";  break;
		case MMOP_FSUB:  opn = "fsub";  break;
		case MMOP_FMUL:  opn = "fmul";  break;
		case MMOP_FDIV:  opn = "fdiv";  break;
		case MMOP_FNEG:  opn = "fneg";  break;
		default:         opn = "fsqrt"; break;
		}
		if (op == MMOP_FNEG || op == MMOP_FSQRT) {
			fload_scratch(f, s0, FR_A);
			fprintf(f, "\t%s\t%s%s, %s%s\n",
			        opn, w, FR_A, w, FR_A);
		} else {
			fload_scratch(f, s0, FR_A);
			fload_scratch(f, s1, FR_B);
			fprintf(f, "\t%s\t%s%s, %s%s, %s%s\n",
			        opn, w, FR_A, w, FR_A, w, FR_B);
		}
		fstore_scratch(f, d, FR_A);
		return;
	}
	case MMOP_CVTSS2SD:   /* f32 -> f64 */
		fload_scratch(f, s0, FR_A);
		fputs("\tfcvt\td16, s16\n", f);
		fstore_scratch(f, d, FR_A);
		return;
	case MMOP_CVTSD2SS:   /* f64 -> f32 */
		fload_scratch(f, s0, FR_A);
		fputs("\tfcvt\ts16, d16\n", f);
		fstore_scratch(f, d, FR_A);
		return;
	case MMOP_CVTTSS2SI:  /* f32 -> i32/i64 (trunc) */
	case MMOP_CVTTSD2SI: {
		const char *ww = (op == MMOP_CVTTSS2SI) ? "s" : "d";
		const char *rw = (in->dtype == MT_I32) ? "w" : "x";
		fload_scratch(f, s0, FR_A);
		fprintf(f, "\tfcvtzs\t%s9, %s%s\n", rw, ww, FR_A);
		scratch_to_dst(f, d, "x9");
		return;
	}
	case MMOP_CVTSI2SS:   /* i32/i64 -> f32 */
	case MMOP_CVTSI2SD: {
		const char *ww = (op == MMOP_CVTSI2SS) ? "s" : "d";
		const char *rw = (s0 && s0->type == MT_I32) ? "w" : "x";
		mv_to_scratch(f, s0, "x9");
		fprintf(f, "\tscvtf\t%s%s, %s9\n", ww, FR_A, rw);
		fstore_scratch(f, d, FR_A);
		return;
	}
	case MMOP_CVTSI2SS_U: /* unsigned i32/i64 -> f32 */
	case MMOP_CVTSI2SD_U: {
		const char *ww = (op == MMOP_CVTSI2SS_U) ? "s" : "d";
		const char *rw = (s0 && s0->type == MT_I32) ? "w" : "x";
		mv_to_scratch(f, s0, "x9");
		fprintf(f, "\tucvtf\t%s%s, %s9\n", ww, FR_A, rw);
		fstore_scratch(f, d, FR_A);
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
			mv_to_scratch(f, s0, "x9");
			fputs("\tadd\tx9, x9, #15\n"
			      "\tand\tx9, x9, #-16\n"
			      "\tsub\tsp, sp, x9\n"
			      "\tmov\tx9, sp\n", f);
			scratch_to_dst(f, d, "x9");
			if (g_fm)
				g_fm->dynalloc = true;
			return;
		}
		g_alloca_cur -= alloca_size_ins(in);
		load_imm(f, "x9", g_alloca_cur + g_slot_base);
		fputs("\tadd\tx9, x29, x9\n", f);
		scratch_to_dst(f, d, "x9");
		return;
	}
	case MMOP_SALLOC: {
		int64_t sz = in->cst ? in->cst->u.i : 0;
		load_imm(f, "x9", sz);
		fputs("\tsub\tsp, sp, x9\n", f);
		return;
	}
	case MMOP_JMP:
		return;   /* handled by the block loop */
	case MMOP_JCC: {
		MVal *a = in->src[0];
		MVal *b = in->src[1];
		const char *ra;
		/* b == const 0: boolean branch (cbnz/cbz on a) */
		if (b && b->kind == MV_CONST && b->con->u.i == 0) {
			ra = branch_src(f, a, "x9");
			fprintf(f, "\t%s\t%s, .L%s.bb%u\n",
			        in->cc == MCC_NE ? "cbnz" : "cbz", ra,
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			return;
		}
		if (a && a->kind == MV_CONST && a->con->u.i == 0) {
			ra = branch_src(f, b, "x9");
			fprintf(f, "\t%s\t%s, .L%s.bb%u\n",
			        in->cc == MCC_NE ? "cbz" : "cbnz", ra,
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			return;
		}
		/* generic register compare: cmp + b.cc */
		{
			const char *rb = branch_src(f, b, "x10");
			ra = branch_src(f, a, "x9");
			fputs("\tcmp\t", f);
			fputs(ra, f);
			fputs(", ", f);
			fputs(rb, f);
			fputs("\n", f);
			fprintf(f, "\tb.%s\t.L%s.bb%u\n", a64_cc_suffix(in->cc),
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
		}
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL)
			fprintf(f, "\tbl\t%s\n", s0->sym ? s0->sym : "?");
		else if (s0) {
			mv_to_scratch(f, s0, "x9");
			fputs("\tblr\tx9\n", f);
		}
		if (d) {
			if (d->type == MT_F32 || d->type == MT_F64) {
				/* float return in v0 (s0/d0) */
				fstore_scratch(f, d, "0");
			} else {
				scratch_to_dst(f, d, "x0");
			}
		}
		return;
	}
	case MMOP_BLIT: {
		/* aggregate copy: src[1] -> src[0], cst bytes.  Unrolled 8-byte
		 * chunks with a byte remainder (sizes are struct smalls). */
		int64_t sz = in->cst ? in->cst->u.i : 0;
		if (sz <= 0)
			return;
		mv_to_scratch(f, s0, "x9");   /* dst pointer */
		mv_to_scratch(f, s1, "x10");  /* src pointer */
		int64_t n8 = sz / 8;
		for (int64_t i = 0; i < n8; i++) {
			fprintf(f, "\tldr\tx11, [x10, #%lld]\n"
			            "\tstr\tx11, [x9, #%lld]\n",
			        (long long)(i * 8), (long long)(i * 8));
		}
		for (int64_t i = n8 * 8; i < sz; i++)
			fprintf(f, "\tldrb\tw11, [x10, #%lld]\n"
			            "\tstrb\tw11, [x9, #%lld]\n",
			        (long long)i, (long long)i);
		return;
	}
	case MMOP_RET:
		return;   /* handled by the block loop */
	case MMOP_NOP:
		return;
	default:
		fprintf(f, "\t# unsupported MMOP %d\n", (int)op);
		return;
	}
}

static int csave_idx(const MTargetM *mt, int r);

/* Callee-saved register operand: GPRs keep their name, V regs use the d64
 * view (AAPCS64 only preserves the low 64 bits of v8-v15). */
static const char *
csave_reg_name(const MTargetM *mt, int r)
{
	static char buf[16];
	if (r >= A64MREG_V0) {
		snprintf(buf, sizeof buf, "d%d", r - A64MREG_V0);
		return buf;
	}
	return mreg_name(mt, r);
}

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
		/* return value move: x0 (int) / v0 (float) */
		if (t.src[0] && (t.src[0]->type == MT_F32 || t.src[0]->type == MT_F64)) {
			fload_scratch(f, t.src[0], "0");   /* -> v0 (s0/d0) */
		} else if (t.src[0]) {
			mv_to_scratch(f, t.src[0], "x0");
		}
		/* restore callee-saved registers in save order */
		for (int i = 0; g_mt->rclob && g_mt->rclob[i] >= 0; i++)
			if ((g_fm->regsused >> g_mt->rclob[i]) & 1) {
				int off = -24 - 8 * csave_idx(g_mt, g_mt->rclob[i]);
				if (off >= -256 && off <= 255)
					fprintf(f, "\tldr\t%s, [x29, #%d]\n",
					        csave_reg_name(g_mt, g_mt->rclob[i]), off);
				else {
					load_imm(f, "x16", off);
					fprintf(f, "\tldr\t%s, [x29, x16]\n",
					        csave_reg_name(g_mt, g_mt->rclob[i]));
				}
			}
		fputs("\tldr\tx30, [x29, #-8]\n", f);
		fputs("\tadd\tsp, x29, #0\n", f);
		fputs("\tldr\tx29, [x29, #-16]\n", f);
		fputs("\tret\n", f);
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
mfnm_emit_aarch64(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	g_fm = fm;
	g_fname = fm->name ? fm->name : "?";
	g_slot_base = -16;      /* fp (8) + lr (8) save area above the slots */

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

	int csaves = 0;
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> fm->mt->rclob[i]) & 1)
			csaves++;

	int framesize = fm->slot + 16 + csaves * 8 + alloca_total(fm);
	framesize = (framesize + 15) & ~15;
	g_alloca_cur = -(fm->slot + 16 + csaves * 8);

	fprintf(f, ".text\n");
	if (fm->name) {
		if (fm->host && fm->host->export)
			fprintf(f, ".globl %s\n", fm->name);
		fprintf(f, "%s:\n", fm->name);
	}
	fprintf(f, "\tsub\tsp, sp, #%d\n", framesize);
	fprintf(f, "\tstp\tx29, x30, [sp, #%d]\n", framesize - 16);
	fprintf(f, "\tadd\tx29, sp, #%d\n", framesize);
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> fm->mt->rclob[i]) & 1) {
			int off = -24 - 8 * csave_idx(fm->mt, fm->mt->rclob[i]);
			if (off >= -256 && off <= 255)
				fprintf(f, "\tstr\t%s, [x29, #%d]\n",
				        csave_reg_name(fm->mt, fm->mt->rclob[i]), off);
			else {
				load_imm(f, "x16", off);
				fprintf(f, "\tstr\t%s, [x29, x16]\n",
				        csave_reg_name(fm->mt, fm->mt->rclob[i]));
			}
		}

	for (MBlkM *b = fm->link; b; b = b->link)
		emit_block(f, b);
}
