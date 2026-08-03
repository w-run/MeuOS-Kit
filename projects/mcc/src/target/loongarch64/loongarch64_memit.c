/* loongarch64_memit.c — loongarch64 machine-layer assembly emission
 * (MIR-native).
 *
 * Emits the lowered MMOP machine IR as loongarch64 (LP64D) assembly.
 * Like riscv64, loongarch64 has no condition flags: comparisons are
 * register-based (MMOP_SETCCR → slt/sltu derivations), branches compare
 * registers directly (JCC carries src[0]/src[1]; bne/beq vs $zero for
 * boolean tests), and there is no conditional move.
 *
 * Value operands are always materialized into the scratch registers
 * t0/t1 before use.  The width suffix is `.w` for 32-bit ops (which
 * sign-extend the result) and `.d` for 64-bit ops.
 *
 * Frame: the prologue saves ra/fp/s-regs above the spill+alloca slots,
 * so every slot is addressed `off(fp)` with off = v->slot - 16 (the 16
 * bytes of the ra+fp save area that the regalloc's savesz does not cover).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "loongarch64_m.h"

static const MTargetM *g_mt;
static MFnM *g_fm;
static int g_alloca_cur;   /* frame-relative cursor for static allocas */
static const char *g_fname;
static int g_slot_base;    /* -16: ra+fp save area above the slots */

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
	if (getenv("MCC_DEBUG_LA")) {
		fprintf(stderr, "alloca_size_ins: cst=%p kind=%d val=%lld src0=%p\n",
		        (void*)in->cst, in->cst ? in->cst->kind : -1,
		        in->cst ? (long long)in->cst->u.i : -1,
		        (void*)in->src[0]);
	}
	if (in->cst && in->cst->kind == MC_INT)
		return (int)in->cst->u.i;
	/* the size may be a constant operand (src[0] = MV_CONST) */
	if (in->src[0] && in->src[0]->kind == MV_CONST && in->src[0]->con &&
	    in->src[0]->con->kind == MC_INT)
		return (int)in->src[0]->con->u.i;
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

static bool
is64(MType t)
{
	return t == MT_I64 || t == MT_PTR;
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

/* Print a register (with the $ prefix), or a slot as fp-relative memory
 * (only valid as a load/store operand). */
static void
emit_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("$zero", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "$%s", mreg_name(g_mt, v->reg));
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "$%s", mreg_name(g_mt, v->reg));
		else
			fprintf(f, "$fp, %d", v->slot + g_slot_base);
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s", v->sym ? v->sym : "0");
		break;
	default:
		fputs("$zero", f);
		break;
	}
}

static void emit_tls_addr(FILE *f, const char *sym, bool isext,
                          const char *rn);

/* Load a value into a scratch register ("$t0"/"$t1").  Handles
 * immediates (li.d) and slots (ld.d off($fp)). */
static void
mv_to_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\tor\t%s, $zero, $zero\n", rn);
		return;
	}
	switch (v->kind) {
	case MV_CONST:
		fprintf(f, "\tli.d\t%s, %lld\n", rn, (long long)(v->con ? v->con->u.i : 0));
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "\tor\t%s, $%s, $zero\n", rn, mreg_name(g_mt, v->reg));
		else
			fprintf(f, "\tld.d\t%s, $fp, %d\n", rn, v->slot + g_slot_base);
		break;
	case MV_REG:
		fprintf(f, "\tor\t%s, $%s, $zero\n", rn, mreg_name(g_mt, v->reg));
		break;
	case MV_GLOBAL:
		if (v->tls)
			emit_tls_addr(f, v->sym ? v->sym : "0", v->isext, rn);
		else
			fprintf(f, "\tpcaddu12i\t%s, %%pc_hi20(%s)\n"
			            "\taddi.d\t%s, %s, %%pc_lo12(%s)\n",
			        rn, v->sym ? v->sym : "0", rn, rn, v->sym ? v->sym : "0");
		break;
	default:
		fprintf(f, "\tor\t%s, $zero, $zero\n", rn);
		break;
	}
}

/* Emit the address of a TLS global into the scratch register rn:
 *   - local-exec (internal symbol): lu12i.w %le_hi20 + ori %le_lo12 +
 *     lu32i.d %le64_lo20 + lu52i.d %le64_hi12 + add.d $tp;
 *   - initial-exec (external): pcalau12i %ie_pc_hi20 + ld.d %ie_pc_lo12 +
 *     add.d $tp. */
static void
emit_tls_addr(FILE *f, const char *sym, bool isext, const char *rn)
{
	if (isext) {
		fprintf(f, "\tpcalau12i\t%s, %%ie_pc_hi20(%s)\n"
		            "\tld.d\t%s, %s, %%ie_pc_lo12(%s)\n"
		            "\tadd.d\t%s, %s, $tp\n",
		        rn, sym, rn, rn, sym, rn, rn);
		return;
	}
	fprintf(f, "\tlu12i.w\t%s, %%le_hi20(%s)\n"
	            "\tori\t%s, %s, %%le_lo12(%s)\n"
	            "\tlu32i.d\t%s, %%le64_lo20(%s)\n"
	            "\tlu52i.d\t%s, %s, %%le64_hi12(%s)\n"
	            "\tadd.d\t%s, %s, $tp\n",
	        rn, sym, rn, rn, sym, rn, sym, rn, rn, sym, rn, rn);
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
			fprintf(f, "\tor\t$%s, %s, $zero\n", mreg_name(g_mt, d->reg), rn);
		else
			fprintf(f, "\tst.d\t%s, $fp, %d\n", rn, d->slot + g_slot_base);
		break;
	case MV_REG:
		fprintf(f, "\tor\t$%s, %s, $zero\n", mreg_name(g_mt, d->reg), rn);
		break;
	default:
		break;
	}
}

/* float precision suffix: .s for f32, .d for f64 */
static const char *
fp_sfx(MType t)
{
	return t == MT_F32 ? "s" : "d";
}

/* Load a float value into the FPR scratch register $f28. */
static void
fmv_to_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\tfmov.%s\t%s, $zero\n", rn[1] == 'f' ? fp_sfx(MT_F64) : "d", rn);
		return;
	}
	switch (v->kind) {
	case MV_CONST: {
		if (v->con->kind == MC_FLT) {
			uint32_t bits;
			memcpy(&bits, &v->con->u.s, 4);
			fprintf(f, "\tli.d\t$t0, 0x%x\n", bits);
			fprintf(f, "\tmovgr2fr.w\t%s, $t0\n", rn);
		} else {
			uint64_t bits;
			memcpy(&bits, &v->con->u.d, 8);
			fprintf(f, "\tli.d\t$t0, 0x%llx\n",
			        (unsigned long long)bits);
			fprintf(f, "\tmovgr2fr.d\t%s, $t0\n", rn);
		}
		return;
	}
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "\tfmov.%s\t%s, $%s\n",
			        fp_sfx(v->type), rn, mreg_name(g_mt, v->reg));
		else
			fprintf(f, "\tf%s\t%s, $fp, %d\n",
			        v->type == MT_F32 ? "ld.s" : "ld.d", rn,
			        v->slot + g_slot_base);
		return;
	case MV_REG:
		fprintf(f, "\tfmov.%s\t%s, $%s\n",
		        fp_sfx(v->type), rn, mreg_name(g_mt, v->reg));
		return;
	default:
		fprintf(f, "\tfmov.d\t%s, $zero\n", rn);
		return;
	}
}

/* Write a scratch FPR back into the destination float value. */
static void
scratch_to_dst_f(FILE *f, MVal *d, const char *rn)
{
	if (!d)
		return;
	switch (d->kind) {
	case MV_TEMP:
		if (d->reg >= 0)
			fprintf(f, "\tfmov.%s\t$%s, %s\n",
			        fp_sfx(d->type), mreg_name(g_mt, d->reg), rn);
		else
			fprintf(f, "\tf%s\t%s, $fp, %d\n",
			        d->type == MT_F32 ? "st.s" : "st.d", rn,
			        d->slot + g_slot_base);
		break;
	case MV_REG:
		fprintf(f, "\tfmov.%s\t$%s, %s\n",
		        fp_sfx(d->type), mreg_name(g_mt, d->reg), rn);
		break;
	default:
		break;
	}
}

/* ---- load/store addressing ---------------------------------------------- */

/* rn = rn + off, handling the 12-bit immediate range with a li.d/add.d
 * sequence for large displacements (e.g. big static allocas). */
static void
emit_offset(FILE *f, const char *rn, const char *basename, int64_t off)
{
	if (off >= -2048 && off <= 2047)
		fprintf(f, "\taddi.d\t%s, %s, %lld\n", rn, basename, (long long)off);
	else {
		fprintf(f, "\tli.d\t%s, %lld\n", rn, (long long)off);
		fprintf(f, "\tadd.d\t%s, %s, %s\n", rn, rn, basename);
	}
}

/* Emit an address into the scratch register rn ("$t0"/"$t1"). */
static void
emit_addr_to_scratch(FILE *f, MAddr a, const char *rn)
{
	MVal *base = a.base;
	if (a.offcon) {
		const char *sym = a.offcon->u.addr.sym;
		fprintf(f, "\tpcaddu12i\t%s, %%pc_hi20(%s)\n", rn, sym);
		fprintf(f, "\taddi.d\t%s, %s, %%pc_lo12(%s)\n", rn, rn, sym);
		if (a.off)
			emit_offset(f, rn, rn, a.off);
		return;
	}
	if (base && base->kind == MV_TEMP && base->reg < 0) {
		/* base is a spilled temp whose stack slot holds a POINTER VALUE
		 * (alloca result / computed address) — load it, then add the
		 * displacement.  Treating the slot as a frame address here made
		 * va_arg's register/stack select read the wrong memory. */
		fprintf(f, "\tld.d\t%s, $fp, %d\n", rn, base->slot + g_slot_base);
		if (a.off)
			emit_offset(f, rn, rn, a.off);
		return;
	}
	if (base && base->kind == MV_CONST) {
		emit_offset(f, rn, "$zero", base->con->u.i + a.off);
		return;
	}
	if (base && base->kind == MV_GLOBAL) {
		const char *sym = base->sym ? base->sym : "0";
		if (base->tls)
			emit_tls_addr(f, sym, base->isext, rn);
		else {
			fprintf(f, "\tpcaddu12i\t%s, %%pc_hi20(%s)\n", rn, sym);
			fprintf(f, "\taddi.d\t%s, %s, %%pc_lo12(%s)\n", rn, rn, sym);
		}
		if (a.off)
			emit_offset(f, rn, rn, a.off);
		return;
	}
	if (!base) {
		emit_offset(f, rn, "$zero", a.off);
		return;
	}
	if (base->kind == MV_REG ||
	    (base->kind == MV_TEMP && base->reg >= 0)) {
		char rname[16];
		snprintf(rname, sizeof rname, "$%s", mreg_name(g_mt, base->reg));
		emit_offset(f, rn, rname, a.off);
		return;
	}
	mv_to_scratch(f, base, rn);
	if (a.off)
		emit_offset(f, rn, rn, a.off);
}

/* ---- condition handling -------------------------------------------------- */

/* branch mnemonic for a two-register compare; swaps operands for the
 * GT/LE forms (loongarch has blt/bge only).  Returns the mnemonic. */
static const char *
rv_cc_suffix(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return "beq";
	case MCC_NE:  return "bne";
	case MCC_LT:  return "blt";
	case MCC_GE:  return "bge";
	case MCC_GT:  return "blt";    /* operands swapped */
	case MCC_LE:  return "bge";    /* operands swapped */
	case MCC_CC:  return "bltu";
	case MCC_CS:  return "bgeu";
	case MCC_HI:  return "bltu";   /* operands swapped */
	case MCC_LS:  return "bgeu";   /* operands swapped */
	default:      return "beq";
	}
}

/* dst = (src[0] cc src[1]) ? 1 : 0 (register comparison). */
static void
emit_setccr(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	if ((a && (a->type == MT_F32 || a->type == MT_F64)) ||
	    (b && (b->type == MT_F32 || b->type == MT_F64))) {
		/* floating-point comparison: fcmp.<cond>.<s/d> $fcc0, fa, fb
		 * writes the integer result into $fcc0; movcf2gr moves it to a
		 * GPR (not an FPR). */
		const char *sx = (a && a->type == MT_F32) ? "s" : "d";
		const char *cn = (cc == MCC_LT || cc == MCC_GT) ? "clt" :
		                 (cc == MCC_LE || cc == MCC_GE) ? "cle" : "ceq";
		bool swap = cc == MCC_GT || cc == MCC_GE;
		fmv_to_scratch(f, a, "$f28");
		fmv_to_scratch(f, b, "$f29");
		fprintf(f, "\tfcmp.%s.%s\t$fcc0, %s, %s\n", cn, sx,
		        swap ? "$f29" : "$f28", swap ? "$f28" : "$f29");
		fputs("\tmovcf2gr\t$t0, $fcc0\n", f);
		if (cc == MCC_NE)
			fputs("\txori\t$t0, $t0, 1\n", f);
		fputs("\tadd.w\t$t0, $t0, $zero\n", f);   /* i32 bool */
		scratch_to_dst(f, in->dst, "$t0");
		return;
	}
	mv_to_scratch(f, a, "$t0");
	mv_to_scratch(f, b, "$t1");
	switch (cc) {
	case MCC_LT:  fputs("\tslt\t$t0, $t0, $t1\n", f); break;
	case MCC_GE:  fputs("\tslt\t$t0, $t0, $t1\n\txori\t$t0, $t0, 1\n", f); break;
	case MCC_GT:  fputs("\tslt\t$t0, $t1, $t0\n", f); break;
	case MCC_LE:  fputs("\tslt\t$t0, $t1, $t0\n\txori\t$t0, $t0, 1\n", f); break;
	case MCC_CC:  fputs("\tsltu\t$t0, $t0, $t1\n", f); break;
	case MCC_CS:  fputs("\tsltu\t$t0, $t0, $t1\n\txori\t$t0, $t0, 1\n", f); break;
	case MCC_HI:  fputs("\tsltu\t$t0, $t1, $t0\n", f); break;
	case MCC_LS:  fputs("\tsltu\t$t0, $t1, $t0\n\txori\t$t0, $t0, 1\n", f); break;
	case MCC_EQ:  fputs("\txor\t$t0, $t0, $t1\n\tsltui\t$t0, $t0, 1\n", f); break;
	case MCC_NE:  fputs("\txor\t$t0, $t0, $t1\n\tsltu\t$t0, $zero, $t0\n", f); break;
	default:      fputs("\tslt\t$t0, $t0, $t1\n", f); break;
	}
	fputs("\tadd.w\t$t0, $t0, $zero\n", f);   /* SETCCR yields an i32 bool */
	scratch_to_dst(f, in->dst, "$t0");
}

/* ---- binary op table ----------------------------------------------------- */

static const char *
binop_name(MMOP op, bool isd)
{
	const char *suf = isd ? ".d" : ".w";
	switch (op) {
	case MMOP_ADD:  return isd ? "add.d" : "add.w";
	case MMOP_SUB:  return isd ? "sub.d" : "sub.w";
	case MMOP_MUL:  return isd ? "mul.d" : "mul.w";
	case MMOP_AND:  return "and";
	case MMOP_OR:   return "or";
	case MMOP_XOR:  return "xor";
	case MMOP_SHL:  return isd ? "sll.d" : "sll.w";
	case MMOP_SHR:  return isd ? "srl.d" : "srl.w";
	case MMOP_SAR:  return isd ? "sra.d" : "sra.w";
	case MMOP_DIV:  return isd ? "div.d" : "div.w";
	case MMOP_UDIV: return isd ? "div.du" : "div.wu";
	case MMOP_REM:  return isd ? "mod.d" : "mod.w";
	case MMOP_UREM: return isd ? "mod.du" : "mod.wu";
	default:        (void)suf; return "add.d";
	}
}

static bool
binop_has_imm(MMOP op)
{
	return op == MMOP_ADD || op == MMOP_AND || op == MMOP_OR ||
	       op == MMOP_XOR || op == MMOP_SHL || op == MMOP_SHR ||
	       op == MMOP_SAR;
}

/* ---- main emission ------------------------------------------------------- */

/* Branch operand: return a register name; load slot/const values into a
 * scratch register first.  Caller provides a buffer for register names. */
static const char *
branch_src(FILE *f, MVal *v, const char *rn, char *buf, size_t bufsz)
{
	if (!v)
		return "$zero";
	if (v->kind == MV_REG) {
		snprintf(buf, bufsz, "$%s", mreg_name(g_mt, v->reg));
		return buf;
	}
	if (v->kind == MV_TEMP && v->reg >= 0) {
		snprintf(buf, bufsz, "$%s", mreg_name(g_mt, v->reg));
		return buf;
	}
	mv_to_scratch(f, v, rn);
	return rn;
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
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			fmv_to_scratch(f, s0, "$f28");
			scratch_to_dst_f(f, d, "$f28");
			return;
		}
		mv_to_scratch(f, s0, "$t0");
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_MOVSX:
		mv_to_scratch(f, s0, "$t0");
		switch (in->dtype) {
		case MT_I8:  fputs("\text.w.b\t$t0, $t0\n", f); break;
		case MT_I16: fputs("\text.w.h\t$t0, $t0\n", f); break;
		case MT_I32: fputs("\tadd.w\t$t0, $t0, $zero\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_MOVZX:
		mv_to_scratch(f, s0, "$t0");
		switch (in->dtype) {
		case MT_I8:  fputs("\tbstrpick.d\t$t0, $t0, 7, 0\n", f); break;
		case MT_I16: fputs("\tbstrpick.d\t$t0, $t0, 15, 0\n", f); break;
		case MT_I32: fputs("\tbstrpick.d\t$t0, $t0, 31, 0\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_LEA:
		emit_addr_to_scratch(f, in->addr, "$t0");
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR:
	case MMOP_DIV: case MMOP_UDIV: case MMOP_REM: case MMOP_UREM: {
		bool isd = is64(in->dtype);
		const char *opn = binop_name(op, isd);
		mv_to_scratch(f, s0, "$t0");
		if (binop_has_imm(op) && s1 && s1->kind == MV_CONST) {
			int64_t v = s1->con->u.i;
			if (v >= -2048 && v <= 2047) {
				if (op == MMOP_ADD)
					fprintf(f, "\taddi.d\t$t0, $t0, %lld\n", (long long)v);
				else
					fprintf(f, "\tli.d\t$t1, %lld\n\t%s\t$t0, $t0, $t1\n",
					        (long long)v, opn);
			} else {
				fprintf(f, "\tli.d\t$t1, %lld\n", (long long)v);
				fprintf(f, "\t%s\t$t0, $t0, $t1\n", opn);
			}
		} else {
			mv_to_scratch(f, s1, "$t1");
			fprintf(f, "\t%s\t$t0, $t0, $t1\n", opn);
		}
		scratch_to_dst(f, d, "$t0");
		return;
	}
	case MMOP_NEG:
		mv_to_scratch(f, s0, "$t0");
		fprintf(f, "\t%s\t$t0, $zero, $t0\n", is64(in->dtype) ? "sub.d" : "sub.w");
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_NOT:
		mv_to_scratch(f, s0, "$t0");
		fputs("\txori\t$t0, $t0, -1\n", f);
		scratch_to_dst(f, d, "$t0");
		return;
	case MMOP_FADD: case MMOP_FSUB: case MMOP_FMUL: case MMOP_FDIV: {
		const char *opn = op == MMOP_FADD ? "fadd" :
		                  op == MMOP_FSUB ? "fsub" :
		                  op == MMOP_FMUL ? "fmul" : "fdiv";
		const char *sx = fp_sfx(in->dtype);
		fmv_to_scratch(f, s0, "$f28");
		fmv_to_scratch(f, s1, "$f29");
		fprintf(f, "\t%s.%s\t$f28, $f28, $f29\n", opn, sx);
		scratch_to_dst_f(f, d, "$f28");
		return;
	}
	case MMOP_FNEG:
		fmv_to_scratch(f, s0, "$f28");
		fprintf(f, "\tfneg.%s\t$f28, $f28\n", fp_sfx(in->dtype));
		scratch_to_dst_f(f, d, "$f28");
		return;
	case MMOP_FSQRT:
		fmv_to_scratch(f, s0, "$f28");
		fprintf(f, "\tfsqrt.%s\t$f28, $f28\n", fp_sfx(in->dtype));
		scratch_to_dst_f(f, d, "$f28");
		return;
	case MMOP_CVTSI2SS: case MMOP_CVTSI2SD:
	case MMOP_CVTSI2SS_U: case MMOP_CVTSI2SD_U: {
		/* int -> fp: GPR source, FPR dst.  The toolchain assembler lacks
		 * the unsigned ffint.*.*u forms; a 32-bit unsigned source is
		 * zero-extended in the GPR, so the signed conversion is exact. */
		const char *sx = (op == MMOP_CVTSI2SS || op == MMOP_CVTSI2SS_U)
		                 ? "s" : "d";
		const char *sw = "w";
		if (s0 && (s0->type == MT_I64 || s0->type == MT_PTR))
			sw = "l";
		mv_to_scratch(f, s0, "$t0");
		fprintf(f, "\tmovgr2fr.%s\t$f28, $t0\n", sw[0] == 'l' ? "d" : "w");
		fprintf(f, "\tffint.%s.%s\t$f28, $f28\n", sx, sw);
		scratch_to_dst_f(f, d, "$f28");
		return;
	}
	case MMOP_CVTTSS2SI: case MMOP_CVTTSD2SI: {
		/* fp -> int (round toward zero = C cast) */
		const char *sx = op == MMOP_CVTTSS2SI ? "s" : "d";
		const char *dw = (d && (d->type == MT_I64 || d->type == MT_PTR))
		                 ? "l" : "w";
		fmv_to_scratch(f, s0, "$f28");
		fprintf(f, "\tftintrz.%s.%s\t$f28, $f28\n", dw, sx);
		fprintf(f, "\tmovfr2gr.%s\t$t0, $f28\n", dw[0] == 'l' ? "d" : "s");
		scratch_to_dst(f, d, "$t0");
		return;
	}
	case MMOP_CVTSS2SD:
		fmv_to_scratch(f, s0, "$f28");
		fputs("\tfcvt.d.s\t$f28, $f28\n", f);
		scratch_to_dst_f(f, d, "$f28");
		return;
	case MMOP_CVTSD2SS:
		fmv_to_scratch(f, s0, "$f28");
		fputs("\tfcvt.s.d\t$f28, $f28\n", f);
		scratch_to_dst_f(f, d, "$f28");
		return;
	case MMOP_BLIT: {
		/* aggregate copy: src[1] -> src[0], cst bytes */
		int64_t sz = in->cst ? in->cst->u.i : 0;
		if (sz <= 0)
			return;
		mv_to_scratch(f, s0, "$t0");   /* dst pointer */
		mv_to_scratch(f, s1, "$t1");   /* src pointer */
		int64_t n8 = sz / 8;
		for (int64_t i = 0; i < n8; i++) {
			fprintf(f, "\tld.d\t$t2, $t1, %lld\n\tst.d\t$t2, $t0, %lld\n",
			        (long long)(i * 8), (long long)(i * 8));
		}
		for (int64_t i = n8 * 8; i < sz; i++)
			fprintf(f, "\tld.b\t$t2, $t1, %lld\n\tst.b\t$t2, $t0, %lld\n",
			        (long long)i, (long long)i);
		return;
	}
	case MMOP_SETCCR:
		emit_setccr(f, in);
		return;
	case MMOP_LOAD:
	case MMOP_LOAD_S8: case MMOP_LOAD_S16: case MMOP_LOAD_S32:
	case MMOP_LOAD_Z8: case MMOP_LOAD_Z16: case MMOP_LOAD_Z32: {
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			const char *lw = in->dtype == MT_F32 ? "fld.s" : "fld.d";
			emit_addr_to_scratch(f, in->addr, "$t0");
			fprintf(f, "\t%s\t$f28, $t0, 0\n", lw);
			scratch_to_dst_f(f, d, "$f28");
			return;
		}
		const char *lw = "ld.d";
		switch (op) {
		case MMOP_LOAD:      lw = is64(in->dtype) ? "ld.d" : "ld.w"; break;
		case MMOP_LOAD_S8:   lw = "ld.b"; break;
		case MMOP_LOAD_S16:  lw = "ld.h"; break;
		case MMOP_LOAD_S32:  lw = "ld.w"; break;
		case MMOP_LOAD_Z8:   lw = "ld.bu"; break;
		case MMOP_LOAD_Z16:  lw = "ld.hu"; break;
		case MMOP_LOAD_Z32:  lw = "ld.wu"; break;
		default: break;
		}
		emit_addr_to_scratch(f, in->addr, "$t0");
		fprintf(f, "\t%s\t$t0, $t0, 0\n", lw);
		scratch_to_dst(f, d, "$t0");
		return;
	}
	case MMOP_STORE: {
		if (in->dtype == MT_F32 || in->dtype == MT_F64) {
			const char *sw = in->dtype == MT_F32 ? "fst.s" : "fst.d";
			/* keep the address and the float value in separate scratch
			 * registers (fmv_to_scratch clobbers $t0 for float consts) */
			emit_addr_to_scratch(f, in->addr, "$t1");
			fmv_to_scratch(f, s0, "$f28");
			fprintf(f, "\t%s\t$f28, $t1, 0\n", sw);
			return;
		}
		const char *sw = is64(in->dtype) ? "st.d" : "st.w";
		emit_addr_to_scratch(f, in->addr, "$t0");
		mv_to_scratch(f, s0, "$t1");
		fprintf(f, "\t%s\t$t1, $t0, 0\n", sw);
		return;
	}
	case MMOP_ALLOCA4: case MMOP_ALLOCA8: case MMOP_ALLOCA16: {
		if (s0 && s0->kind != MV_CONST) {
			mv_to_scratch(f, s0, "$t0");
			fputs("\taddi.d\t$t0, $t0, 15\n"
			      "\tbstrpick.d\t$t0, $t0, 63, 4\n"
			      "\tsub.d\t$sp, $sp, $t0\n"
			      "\tor\t$t0, $sp, $zero\n", f);
			scratch_to_dst(f, d, "$t0");
			if (g_fm)
				g_fm->dynalloc = true;
			return;
		}
		g_alloca_cur -= alloca_size_ins(in);
		emit_offset(f, "$t0", "$fp", g_alloca_cur);
		scratch_to_dst(f, d, "$t0");
		return;
	}
	case MMOP_SALLOC: {
		int64_t sz = in->cst ? in->cst->u.i : 0;
		fprintf(f, "\taddi.d\t$sp, $sp, -%lld\n", (long long)sz);
		return;
	}
	case MMOP_JMP:
		return;
	case MMOP_JCC: {
		MVal *a = in->src[0];
		MVal *b = in->src[1];
		const char *sfx = rv_cc_suffix(in->cc);
		/* b == const 0: boolean branch (bne/beq vs $zero) */
		if (b && b->kind == MV_CONST && b->con->u.i == 0) {
			char rbuf[16];
			const char *ra = branch_src(f, a, "$t0", rbuf, sizeof rbuf);
			fprintf(f, "\t%s\t%s, $zero, .L%s.bb%u\n",
			        in->cc == MCC_NE ? "bne" : "beq", ra,
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			if (in->blk->s2)
				fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, in->blk->s2->id);
			return;
		}
		if (a && a->kind == MV_CONST && a->con->u.i == 0) {
			char rbuf[16];
			const char *rb = branch_src(f, b, "$t0", rbuf, sizeof rbuf);
			fprintf(f, "\t%s\t%s, $zero, .L%s.bb%u\n",
			        in->cc == MCC_NE ? "beq" : "bne", rb,
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			if (in->blk->s2)
				fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, in->blk->s2->id);
			return;
		}
		{
			char rab[16], rbb[16];
			const char *ra = branch_src(f, a, "$t0", rab, sizeof rab);
			const char *rb = branch_src(f, b, "$t1", rbb, sizeof rbb);
			/* GT/LE/GTU/LEU swap operands */
			bool swap = in->cc == MCC_GT || in->cc == MCC_LE ||
			            in->cc == MCC_HI || in->cc == MCC_LS;
			fprintf(f, "\t%s\t%s, %s, .L%s.bb%u\n", sfx,
			        swap ? rb : ra, swap ? ra : rb,
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
		}
		/* blocks are emitted in reverse link order, so the false target
		 * is never the physical next block — branch explicitly */
		if (in->blk->s2)
			fprintf(f, "\tb\t.L%s.bb%u\n", g_fname, in->blk->s2->id);
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL) {
			const char *sym = s0->sym ? s0->sym : "0";
			fprintf(f, "\tpcaddu12i\t$t0, %%pc_hi20(%s)\n", sym);
			fprintf(f, "\taddi.d\t$t0, $t0, %%pc_lo12(%s)\n", sym);
			fputs("\tjirl\t$ra, $t0, 0\n", f);
		} else if (s0) {
			mv_to_scratch(f, s0, "$t0");
			fputs("\tjirl\t$ra, $t0, 0\n", f);
		}
		if (d && !in->td) {
			/* scalar return: land a0/fa0 into the result value.  An
			 * aggregate return (in->td) leaves d as the pad pointer set
			 * up by selcall — a0 holds the first return chunk there and
			 * must NOT clobber the pad. */
			if (d->type == MT_F32 || d->type == MT_F64)
				scratch_to_dst_f(f, d, "$fa0");
			else
				scratch_to_dst(f, d, "$a0");
		}
		return;
	}
	case MMOP_RET:
		return;
	case MMOP_NOP:
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
		/* return value move: a0 */
		if (t.src[0])
			mv_to_scratch(f, t.src[0], "$a0");
		/* restore callee-saved s-regs/f-regs in save order */
		for (int i = 0; g_mt->rclob && g_mt->rclob[i] >= 0; i++)
			if ((g_fm->regsused >> g_mt->rclob[i]) & 1) {
				int off = -24 - 8 * csave_idx(g_mt, g_mt->rclob[i]);
				bool isf = g_mt->rclob[i] >= g_mt->fpr0;
				fprintf(f, "\t%s\t$%s, $fp, %d\n",
				        isf ? "fld.d" : "ld.d",
				        mreg_name(g_mt, g_mt->rclob[i]), off);
			}
		fputs("\tld.d\t$ra, $fp, -8\n", f);
		fputs("\taddi.d\t$sp, $fp, 0\n", f);
		fputs("\tld.d\t$fp, $sp, -16\n", f);
		fputs("\tjirl\t$zero, $ra, 0\n", f);
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
mfnm_emit_loongarch64(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	g_fm = fm;
	g_fname = fm->name ? fm->name : "?";
	g_slot_base = -16;      /* ra (8) + fp (8) save area above the slots */

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
	if (framesize <= 2047)
		fprintf(f, "\taddi.d\t$sp, $sp, -%d\n", framesize);
	else {
		fprintf(f, "\tli.d\t$t8, -%d\n", framesize);
		fprintf(f, "\tadd.d\t$sp, $sp, $t8\n");
	}
	if (framesize - 16 <= 2047) {
		fprintf(f, "\tst.d\t$ra, $sp, %d\n", framesize - 8);
		fprintf(f, "\tst.d\t$fp, $sp, %d\n", framesize - 16);
		fprintf(f, "\taddi.d\t$fp, $sp, %d\n", framesize);
	} else {
		/* large frame: compute the old sp in a scratch register so the
		 * ra/fp saves use small offsets */
		fprintf(f, "\tli.d\t$t8, %d\n", framesize);
		fprintf(f, "\tadd.d\t$t0, $sp, $t8\n");
		fputs("\tst.d\t$ra, $t0, -8\n", f);
		fputs("\tst.d\t$fp, $t0, -16\n", f);
		fputs("\tor\t$fp, $t0, $zero\n", f);
	}
	for (int i = 0; fm->mt->rclob && fm->mt->rclob[i] >= 0; i++)
		if ((fm->regsused >> fm->mt->rclob[i]) & 1) {
			int off = -24 - 8 * csave_idx(fm->mt, fm->mt->rclob[i]);
			bool isf = fm->mt->rclob[i] >= fm->mt->fpr0;
			fprintf(f, "\t%s\t$%s, $fp, %d\n",
			        isf ? "fst.d" : "st.d",
			        mreg_name(fm->mt, fm->mt->rclob[i]), off);
		}

	for (MBlkM *b = fm->link; b; b = b->link)
		emit_block(f, b);
}
