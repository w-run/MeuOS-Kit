/* i386_memit.c — i386 (32-bit) machine-layer assembly emission (MIR-native).
 *
 * Emits the lowered MMOP machine IR as i386 AT&T assembly.  Uses SSE2 for
 * floating-point (movsd/movss for scalar, addsd/addss, etc.).
 *
 * cdecl frame: pushl %ebp; movl %esp, %ebp; subl $N, %esp.
 * Slots addressed as off(%ebp) with off = v->slot - pushbytes.
 *
 * Division: idivl/divl use edx:eax as a register pair — the emitter
 * saves/restores eax/edx around division.  Remainder is in edx after
 * idivl/divl.
 *
 * Scratch registers: eax, ecx, edx, xmm0 (never allocated).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "i386_m.h"

static const MTargetM *g_mt;
static MFnM *g_fm;
static int g_alloca_cur;
static const char *g_fname;
static int g_slot_base;    /* -(pushbytes): push area above slots */

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
		fputs("$0", f);
		return;
	}
	switch (c->kind) {
	case MC_INT:  fprintf(f, "$%lld", (long long)c->u.i); break;
	case MC_FLT:  fprintf(f, "$0x%x", c->u.s); break;
	case MC_ADDR: fprintf(f, "%s", c->u.addr.sym ? c->u.addr.sym : "$0"); break;
	default:      fputs("$0", f); break;
	}
}

/* Print a register, or a slot as ebp-relative memory. */
static void
emit_mval(FILE *f, MVal *v)
{
	if (!v) {
		fputs("%eax", f);
		return;
	}
	switch (v->kind) {
	case MV_REG:
		fprintf(f, "%%%s", mreg_name(g_mt, v->reg));
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "%%%s", mreg_name(g_mt, v->reg));
		else
			fprintf(f, "%d(%%ebp)", v->slot + g_slot_base);
		break;
	case MV_CONST:
		emit_const(f, v->con);
		break;
	case MV_GLOBAL:
		fprintf(f, "%s", v->sym ? v->sym : "0");
		break;
	default:
		fputs("%eax", f);
		break;
	}
}

/* Load a value into a scratch register. */
static void
mv_to_scratch(FILE *f, MVal *v, const char *rn)
{
	if (!v) {
		fprintf(f, "\txorl\t%%%s, %%%s\n", rn, rn);
		return;
	}
	switch (v->kind) {
	case MV_CONST:
		if (v->con && v->con->kind == MC_INT && v->con->u.i == 0)
			fprintf(f, "\txorl\t%%%s, %%%s\n", rn, rn);
		else if (v->con && v->con->kind == MC_INT)
			fprintf(f, "\tmovl\t$%lld, %%%s\n",
			        (long long)v->con->u.i, rn);
		else
			fprintf(f, "\tmovl\t$0, %%%s\n", rn);
		break;
	case MV_TEMP:
		if (v->reg >= 0)
			fprintf(f, "\tmovl\t%%%s, %%%s\n",
			        mreg_name(g_mt, v->reg), rn);
		else {
			int off = v->slot + g_slot_base;
			fprintf(f, "\tmovl\t%d(%%ebp), %%%s\n", off, rn);
		}
		break;
	case MV_REG:
		fprintf(f, "\tmovl\t%%%s, %%%s\n", mreg_name(g_mt, v->reg), rn);
		break;
	case MV_GLOBAL: {
		const char *sym = v->sym ? v->sym : "0";
		fprintf(f, "\tmovl\t$%s, %%%s\n", sym, rn);
		break;
	}
	default:
		fprintf(f, "\txorl\t%%%s, %%%s\n", rn, rn);
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
			fprintf(f, "\tmovl\t%%%s, %%%s\n", rn,
			        mreg_name(g_mt, d->reg));
		else {
			int off = d->slot + g_slot_base;
			fprintf(f, "\tmovl\t%%%s, %d(%%ebp)\n", rn, off);
		}
		break;
	case MV_REG:
		fprintf(f, "\tmovl\t%%%s, %%%s\n", rn,
		        mreg_name(g_mt, d->reg));
		break;
	default:
		break;
	}
}

/* ---- load/store addressing ---------------------------------------------- */

/* Emit the address in MAddr into %ecx as a memory operand string. */
static void
emit_addr_str(FILE *f, MAddr a, char *buf, size_t bufsz)
{
	MVal *base = a.base;
	int64_t off = a.off;
	const char *base_s = "";

	if (base && base->kind == MV_TEMP && base->reg < 0) {
		/* base is a spilled temp — load it into %ecx first */
		mv_to_scratch(f, base, "ecx");
		base_s = "%ecx";
	} else if (base && base->kind == MV_CONST) {
		fprintf(f, "\tmovl\t");
		emit_const(f, base->con);
		fprintf(f, ", %%ecx\n");
		base_s = "%ecx";
	} else if (base && base->kind == MV_GLOBAL) {
		fprintf(f, "\tmovl\t$%s, %%ecx\n", base->sym ? base->sym : "0");
		base_s = "%ecx";
	} else if (base) {
		/* mreg_name returns bare name ("ebp"); AT&T syntax needs %ebp */
		const char *rn = mreg_name(g_mt, base->reg);
		if (!rn || rn[0] == '\0') {
			mv_to_scratch(f, base, "ecx");
			base_s = "%ecx";
		} else {
			/* Use a static buffer to hold the %-prefixed name */
			static char rbuf[16];
			snprintf(rbuf, sizeof rbuf, "%%%s", rn);
			base_s = rbuf;
		}
	}

	if (off == 0 && base) {
		snprintf(buf, bufsz, "(%s)", base_s);
	} else if (base) {
		snprintf(buf, bufsz, "%lld(%s)", (long long)off, base_s);
	} else {
		snprintf(buf, bufsz, "%lld", (long long)off);
	}
}

/* ---- floating point ----------------------------------------------------- */

#define FR_A "xmm0"

static const char *i386_cc_suffix(MCC cc);

/* Load a floating value into xmm0. */
static void
fload_scratch(FILE *f, MVal *v)
{
	if (!v) {
		fputs("\txorpd\t%xmm0, %xmm0\n", f);
		return;
	}
	switch (v->kind) {
	case MV_CONST: {
		MConst *c = v->con;
		if (c && c->kind == MC_FLT) {
			if (c->type == MT_F32) {
				uint32_t bits;
				memcpy(&bits, &c->u.s, 4);
				if (bits == 0)
					fputs("\txorps\t%xmm0, %xmm0\n", f);
				else {
					/* push constant onto stack, load via movss */
					fprintf(f, "\tpushl\t$0x%x\n", bits);
					fputs("\tmovss\t(%esp), %xmm0\n", f);
					fputs("\taddl\t$4, %esp\n", f);
				}
			} else {
				uint64_t bits;
				memcpy(&bits, &c->u.d, 8);
				if (bits == 0)
					fputs("\txorpd\t%xmm0, %xmm0\n", f);
				else {
					/* push 8-byte constant (two 4-byte pushes) */
					fprintf(f, "\tpushl\t$0x%x\n",
					        (uint32_t)(bits >> 32));
					fprintf(f, "\tpushl\t$0x%x\n",
					        (uint32_t)bits);
					fputs("\tmovsd\t(%esp), %xmm0\n", f);
					fputs("\taddl\t$8, %esp\n", f);
				}
			}
			return;
		}
		fputs("\txorpd\t%xmm0, %xmm0\n", f);
		return;
	}
	case MV_TEMP:
		if (v->reg >= 0) {
			if (v->reg >= I386MREG_XMM0)
				fprintf(f, "\tmovsd\t%%%s, %%xmm0\n",
				        mreg_name(g_mt, v->reg));
			else {
				/* GPR holding float bits — movd to xmm0 */
				fprintf(f, "\tmovd\t%%%s, %%xmm0\n",
				        mreg_name(g_mt, v->reg));
			}
		} else {
			int off = v->slot + g_slot_base;
			fprintf(f, "\tmovsd\t%d(%%ebp), %%xmm0\n", off);
		}
		return;
	case MV_REG:
		if (v->reg >= I386MREG_XMM0)
			fprintf(f, "\tmovsd\t%%%s, %%xmm0\n",
			        mreg_name(g_mt, v->reg));
		else
			fprintf(f, "\tmovd\t%%%s, %%xmm0\n",
			        mreg_name(g_mt, v->reg));
		return;
	case MV_GLOBAL:
		fprintf(f, "\tmovsd\t%s, %%xmm0\n", v->sym ? v->sym : "0");
		return;
	default:
		fputs("\txorpd\t%xmm0, %xmm0\n", f);
		return;
	}
}

/* Store xmm0 into the destination value. */
static void
fstore_scratch(FILE *f, MVal *d)
{
	if (!d)
		return;
	switch (d->kind) {
	case MV_TEMP:
		if (d->reg >= 0) {
			if (d->reg >= I386MREG_XMM0)
				fprintf(f, "\tmovsd\t%%xmm0, %%%s\n",
				        mreg_name(g_mt, d->reg));
			else
				fprintf(f, "\tmovd\t%%xmm0, %%%s\n",
				        mreg_name(g_mt, d->reg));
		} else {
			int off = d->slot + g_slot_base;
			fprintf(f, "\tmovsd\t%%xmm0, %d(%%ebp)\n", off);
		}
		return;
	case MV_REG:
		if (d->reg >= I386MREG_XMM0)
			fprintf(f, "\tmovsd\t%%xmm0, %%%s\n",
			        mreg_name(g_mt, d->reg));
		else
			fprintf(f, "\tmovd\t%%xmm0, %%%s\n",
			        mreg_name(g_mt, d->reg));
		return;
	default:
		return;
	}
}

/* dst = (a cc b) ? 1 : 0 for floating operands. */
static void
emit_setccr_fp(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	bool is32 = (a && a->type == MT_F32);
	fload_scratch(f, a);
	if (b) {
		/* load b into xmm1 */
		if (b->kind == MV_CONST) {
			MConst *c = b->con;
			if (c && c->kind == MC_FLT) {
				if (is32) {
					uint32_t bits;
					memcpy(&bits, &c->u.s, 4);
					fprintf(f, "\tpushl\t$0x%x\n", bits);
					fputs("\tmovss\t(%esp), %xmm1\n", f);
					fputs("\taddl\t$4, %esp\n", f);
				} else {
					uint64_t bits;
					memcpy(&bits, &c->u.d, 8);
					fprintf(f, "\tpushl\t$0x%x\n",
					        (uint32_t)(bits >> 32));
					fprintf(f, "\tpushl\t$0x%x\n",
					        (uint32_t)bits);
					fputs("\tmovsd\t(%esp), %xmm1\n", f);
					fputs("\taddl\t$8, %esp\n", f);
				}
			}
		} else if (b->kind == MV_REG || (b->kind == MV_TEMP && b->reg >= 0)) {
			fprintf(f, "\tmovsd\t%%%s, %%xmm1\n",
			        mreg_name(g_mt, b->reg));
		} else {
			int off = b->slot + g_slot_base;
			fprintf(f, "\tmovsd\t%d(%%ebp), %%xmm1\n", off);
		}
	} else {
		fputs("\txorpd\t%xmm1, %xmm1\n", f);
	}
	if (is32)
		fputs("\tucomiss\t%xmm1, %xmm0\n", f);
	else
		fputs("\tucomisd\t%xmm1, %xmm0\n", f);
	fprintf(f, "\tset%s\t%%al\n", i386_cc_suffix(cc));
	fputs("\tmovzbl\t%al, %eax\n", f);
	scratch_to_dst(f, in->dst, "eax");
}

/* ---- condition suffix ---------------------------------------------------- */

static const char *
i386_cc_suffix(MCC cc)
{
	switch (cc) {
	case MCC_EQ:  return "e";
	case MCC_NE:  return "ne";
	case MCC_GE:  return "ge";
	case MCC_LT:  return "l";
	case MCC_GT:  return "g";
	case MCC_LE:  return "le";
	case MCC_CS:  return "b";    /* below (unsigned lt) */
	case MCC_CC:  return "ae";   /* above or equal (unsigned ge) */
	case MCC_HI:  return "a";    /* above (unsigned gt) */
	case MCC_LS:  return "be";   /* below or equal (unsigned le) */
	default:      return "e";
	}
}

/* dst = (src[0] cc src[1]) ? 1 : 0 */
static void
emit_setccr(FILE *f, MInsM *in)
{
	MVal *a = in->src[0];
	MVal *b = in->src[1];
	MCC cc = in->cc;
	mv_to_scratch(f, a, "eax");
	mv_to_scratch(f, b, "ecx");
	fputs("\tcmpl\t%ecx, %eax\n", f);
	fprintf(f, "\tset%s\t%%al\n", i386_cc_suffix(cc));
	fputs("\tmovzbl\t%al, %eax\n", f);
	scratch_to_dst(f, in->dst, "eax");
}

/* ---- binary op table ----------------------------------------------------- */

static const char *
binop_name(MMOP op)
{
	switch (op) {
	case MMOP_ADD:  return "addl";
	case MMOP_SUB:  return "subl";
	case MMOP_MUL:  return "imull";
	case MMOP_AND:  return "andl";
	case MMOP_OR:   return "orl";
	case MMOP_XOR:  return "xorl";
	case MMOP_SHL:  return "shll";
	case MMOP_SHR:  return "shrl";
	case MMOP_SAR:  return "sarl";
	case MMOP_DIV:  return "idivl";    /* signed */
	case MMOP_UDIV: return "divl";     /* unsigned */
	case MMOP_REM:  return "idivl";    /* remainder via idivl, result in edx */
	case MMOP_UREM: return "divl";
	default:        return "addl";
	}
}

/* ---- main emission ------------------------------------------------------- */

static const char *
branch_src(FILE *f, MVal *v, const char *rn)
{
	if (!v)
		return rn;
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
	char addr_buf[64];

	if (dt == MT_F32 || dt == MT_F64) {
		bool is32 = dt == MT_F32;
		emit_addr_str(f, a, addr_buf, sizeof addr_buf);
		if (is32)
			fprintf(f, "\tmovss\t%s, %%xmm0\n", addr_buf);
		else
			fprintf(f, "\tmovsd\t%s, %%xmm0\n", addr_buf);
		fstore_scratch(f, d);
		return;
	}
	emit_addr_str(f, a, addr_buf, sizeof addr_buf);
	const char *lw = "movl";
	switch (op) {
	case MMOP_LOAD_S8:  lw = "movsbl"; break;
	case MMOP_LOAD_S16: lw = "movswl"; break;
	case MMOP_LOAD_Z8:  lw = "movzbl"; break;
	case MMOP_LOAD_Z16: lw = "movzwl"; break;
	default: break;
	}
	fprintf(f, "\t%s\t%s, %%%s\n", lw, addr_buf, "eax");
	scratch_to_dst(f, d, "eax");
}

static void
emit_store(FILE *f, MType dt, MAddr a, MVal *s0)
{
	char addr_buf[64];

	if (dt == MT_F32 || dt == MT_F64) {
		bool is32 = dt == MT_F32;
		fload_scratch(f, s0);
		emit_addr_str(f, a, addr_buf, sizeof addr_buf);
		if (is32)
			fprintf(f, "\tmovss\t%%xmm0, %s\n", addr_buf);
		else
			fprintf(f, "\tmovsd\t%%xmm0, %s\n", addr_buf);
		return;
	}
	emit_addr_str(f, a, addr_buf, sizeof addr_buf);
	mv_to_scratch(f, s0, "eax");
	switch (dt) {
	case MT_I8:  fprintf(f, "\tmovb\t%%al, %s\n", addr_buf); break;
	case MT_I16: fprintf(f, "\tmovw\t%%ax, %s\n", addr_buf); break;
	default:     fprintf(f, "\tmovl\t%%eax, %s\n", addr_buf); break;
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
		if (s0 && (s0->type == MT_F32 || s0->type == MT_F64)) {
			fload_scratch(f, s0);
			fstore_scratch(f, d);
			return;
		}
		mv_to_scratch(f, s0, "eax");
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_MOVSX:
		mv_to_scratch(f, s0, "eax");
		switch (in->dtype) {
		case MT_I8:  fputs("\tmovsbl\t%al, %eax\n", f); break;
		case MT_I16: fputs("\tmovswl\t%ax, %eax\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_MOVZX:
		mv_to_scratch(f, s0, "eax");
		switch (in->dtype) {
		case MT_I8:  fputs("\tmovzbl\t%al, %eax\n", f); break;
		case MT_I16: fputs("\tmovzwl\t%ax, %eax\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_LEA: {
		char addr_buf[64];
		emit_addr_str(f, in->addr, addr_buf, sizeof addr_buf);
		fprintf(f, "\tleal\t%s, %%eax\n", addr_buf);
		scratch_to_dst(f, d, "eax");
		return;
	}
	case MMOP_ADD: case MMOP_SUB: case MMOP_MUL:
	case MMOP_AND: case MMOP_OR:  case MMOP_XOR:
	case MMOP_SHL: case MMOP_SHR: case MMOP_SAR:
	case MMOP_DIV: case MMOP_UDIV: case MMOP_REM: case MMOP_UREM: {
		const char *opn = binop_name(op);
		if (op == MMOP_DIV || op == MMOP_UDIV ||
		    op == MMOP_REM || op == MMOP_UREM) {
			/* Division: sign-extend eax into edx, then idiv/div.
			 * For signed division (idivl), use cdq (sign-extend eax->edx:eax).
			 * For unsigned division (divl), zero-extend (xor edx,edx). */
			mv_to_scratch(f, s0, "eax");
			if (op == MMOP_DIV || op == MMOP_REM)
				fputs("\tcdq\n", f);       /* sign-extend eax -> edx:eax */
			else
				fputs("\txorl\t%edx, %edx\n", f);  /* zero-extend */
			/* divisor goes into ecx */
			mv_to_scratch(f, s1, "ecx");
			fprintf(f, "\t%s\t%%ecx\n", opn);
			/* Quotient in eax, remainder in edx */
			if (op == MMOP_DIV || op == MMOP_UDIV)
				scratch_to_dst(f, d, "eax");
			else
				scratch_to_dst(f, d, "edx");
		} else {
			mv_to_scratch(f, s0, "eax");
			mv_to_scratch(f, s1, "ecx");
			fprintf(f, "\t%s\t%%ecx, %%eax\n", opn);
			scratch_to_dst(f, d, "eax");
		}
		return;
	}
	case MMOP_NEG:
		mv_to_scratch(f, s0, "eax");
		fputs("\tnegl\t%eax\n", f);
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_NOT:
		mv_to_scratch(f, s0, "eax");
		fputs("\tnotl\t%eax\n", f);
		scratch_to_dst(f, d, "eax");
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
		const char *opn;
		switch (op) {
		case MMOP_FADD:  opn = is32 ? "addss"  : "addsd";  break;
		case MMOP_FSUB:  opn = is32 ? "subss"  : "subsd";  break;
		case MMOP_FMUL:  opn = is32 ? "mulss"  : "mulsd";  break;
		case MMOP_FDIV:  opn = is32 ? "divss"  : "divsd";  break;
		case MMOP_FNEG:  opn = "neg";  break;
		default:         opn = "sqrt"; break;
		}
		fload_scratch(f, s0);
		if (op == MMOP_FNEG) {
			/* XOR sign bit */
			if (is32) {
				fputs("\tpushl\t$0x80000000\n", f);
				fputs("\tmovss\t(%esp), %xmm1\n", f);
				fputs("\taddl\t$4, %esp\n", f);
				fputs("\txorps\t%xmm1, %xmm0\n", f);
			} else {
				fputs("\tpushl\t$0x80000000\n", f);
				fputs("\tpushl\t$0x00000000\n", f);
				fputs("\tmovsd\t(%esp), %xmm1\n", f);
				fputs("\taddl\t$8, %esp\n", f);
				fputs("\txorpd\t%xmm1, %xmm0\n", f);
			}
		} else if (op == MMOP_FSQRT) {
			fprintf(f, "\tsqrt%s\t%%xmm0, %%xmm0\n", is32 ? "ss" : "sd");
		} else {
			/* load s1 into xmm1 */
			if (s1) {
				if (s1->kind == MV_CONST) {
					MConst *c = s1->con;
					if (c && c->kind == MC_FLT) {
						if (is32) {
							uint32_t bits;
							memcpy(&bits, &c->u.s, 4);
							fprintf(f, "\tpushl\t$0x%x\n", bits);
							fputs("\tmovss\t(%esp), %xmm1\n", f);
							fputs("\taddl\t$4, %esp\n", f);
						} else {
							uint64_t bits;
							memcpy(&bits, &c->u.d, 8);
							fprintf(f, "\tpushl\t$0x%x\n",
							        (uint32_t)(bits >> 32));
							fprintf(f, "\tpushl\t$0x%x\n",
							        (uint32_t)bits);
							fputs("\tmovsd\t(%esp), %xmm1\n", f);
							fputs("\taddl\t$8, %esp\n", f);
						}
					}
				} else if (s1->kind == MV_REG ||
				           (s1->kind == MV_TEMP && s1->reg >= 0)) {
					fprintf(f, "\tmovsd\t%%%s, %%xmm1\n",
					        mreg_name(g_mt, s1->reg));
				} else {
					int off = s1->slot + g_slot_base;
					fprintf(f, "\tmovsd\t%d(%%ebp), %%xmm1\n", off);
				}
			}
			fprintf(f, "\t%s\t%%xmm1, %%xmm0\n", opn);
		}
		fstore_scratch(f, d);
		return;
	}
	case MMOP_CVTSS2SD:   /* f32 -> f64 */
		fload_scratch(f, s0);
		fputs("\tcvtss2sd\t%xmm0, %xmm0\n", f);
		fstore_scratch(f, d);
		return;
	case MMOP_CVTSD2SS:   /* f64 -> f32 */
		fload_scratch(f, s0);
		fputs("\tcvtsd2ss\t%xmm0, %xmm0\n", f);
		fstore_scratch(f, d);
		return;
	case MMOP_CVTTSS2SI:  /* f32 -> i32 (trunc) */
	case MMOP_CVTTSD2SI: {
		fload_scratch(f, s0);
		if (op == MMOP_CVTTSS2SI)
			fputs("\tcvttss2si\t%xmm0, %eax\n", f);
		else
			fputs("\tcvttsd2si\t%xmm0, %eax\n", f);
		scratch_to_dst(f, d, "eax");
		return;
	}
	case MMOP_CVTSI2SS:   /* i32 -> f32 */
	case MMOP_CVTSI2SD: {
		bool is32 = (op == MMOP_CVTSI2SS);
		mv_to_scratch(f, s0, "eax");
		if (is32)
			fputs("\tcvtsi2ss\t%eax, %xmm0\n", f);
		else
			fputs("\tcvtsi2sd\t%eax, %xmm0\n", f);
		fstore_scratch(f, d);
		return;
	}
	case MMOP_CVTSI2SS_U: /* unsigned i32 -> f32 */
	case MMOP_CVTSI2SD_U: {
		bool is32 = (op == MMOP_CVTSI2SS_U);
		mv_to_scratch(f, s0, "eax");
		/* unsigned conversion: test if sign bit is set, handle via
		 * float conversion trick */
		fputs("\ttestl\t%eax, %eax\n", f);
		fputs("\tjs\t.Lu2f\n", f);
		fprintf(f, "\tcvtsi2%s\t%%eax, %%xmm0\n", is32 ? "ss" : "sd");
		fputs("\tjmp\t.Lu2f_end\n", f);
		fputs(".Lu2f:\n", f);
		/* push %eax, then convert via memory */
		fputs("\tpushl\t%eax\n", f);
		fputs("\tfildl\t(%esp)\n", f);
		fputs("\taddl\t$4, %esp\n", f);
		if (is32)
			fputs("\tfstps\t-4(%esp)\n\tmovss\t-4(%esp), %xmm0\n", f);
		else
			fputs("\tfstpl\t-8(%esp)\n\tmovsd\t-8(%esp), %xmm0\n", f);
		fputs(".Lu2f_end:\n", f);
		fstore_scratch(f, d);
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
			mv_to_scratch(f, s0, "eax");
			fputs("\taddl\t$15, %eax\n"
			      "\tandl\t$-16, %eax\n"
			      "\tsubl\t%eax, %esp\n"
			      "\tmovl\t%esp, %eax\n", f);
			scratch_to_dst(f, d, "eax");
			if (g_fm)
				g_fm->dynalloc = true;
			return;
		}
		g_alloca_cur -= alloca_size_ins(in);
		fprintf(f, "\tleal\t%d(%%ebp), %%eax\n", g_alloca_cur);
		scratch_to_dst(f, d, "eax");
		return;
	}
	case MMOP_SALLOC: {
		int64_t sz = in->cst ? in->cst->u.i : 0;
		fprintf(f, "\tsubl\t$%lld, %%esp\n", (long long)sz);
		return;
	}
	case MMOP_JMP:
		return;   /* handled by the block loop */
	case MMOP_JCC: {
		MVal *a = in->src[0];
		MVal *b = in->src[1];
		if (b && b->kind == MV_CONST && b->con->u.i == 0) {
			const char *ra = branch_src(f, a, "eax");
			fprintf(f, "\tcmpl\t$0, %%%s\n", ra);
			fprintf(f, "\tj%s\t.L%s.bb%u\n",
			        in->cc == MCC_NE ? "ne" : "e",
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			goto jcc_fall;
		}
		if (a && a->kind == MV_CONST && a->con->u.i == 0) {
			const char *rb = branch_src(f, b, "eax");
			fprintf(f, "\tcmpl\t$0, %%%s\n", rb);
			fprintf(f, "\tj%s\t.L%s.bb%u\n",
			        in->cc == MCC_NE ? "e" : "ne",
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
			goto jcc_fall;
		}
		{
			const char *ra = branch_src(f, a, "eax");
			const char *rb = branch_src(f, b, "ecx");
			fprintf(f, "\tcmpl\t%%%s, %%%s\n", rb, ra);
			fprintf(f, "\tj%s\t.L%s.bb%u\n", i386_cc_suffix(in->cc),
			        g_fname, in->blk->s1 ? in->blk->s1->id : 0);
		}
	jcc_fall:
		if (in->blk->s2)
			fprintf(f, "\tjmp\t.L%s.bb%u\n", g_fname, in->blk->s2->id);
		return;
	}
	case MMOP_CALL: {
		if (s0 && s0->kind == MV_GLOBAL) {
			fprintf(f, "\tcall\t%s\n", s0->sym ? s0->sym : "?");
		} else if (s0) {
			mv_to_scratch(f, s0, "eax");
			fprintf(f, "\tcall\t*%%eax\n");
		}
		if (d && !in->td) {
			if (d->type == MT_F32 || d->type == MT_F64) {
				fstore_scratch(f, d);
			} else {
				scratch_to_dst(f, d, "eax");
			}
		}
		return;
	}
	case MMOP_RET:
		return;   /* handled by the block loop */
	case MMOP_NOP:
		return;
	case MMOP_PUSH: {
		if (s0)
			mv_to_scratch(f, s0, "eax");
		fputs("\tpushl\t%eax\n", f);
		return;
	}
	case MMOP_POP: {
		fputs("\tpopl\t%eax\n", f);
		scratch_to_dst(f, d, "eax");
		return;
	}
	default:
		fprintf(f, "\t# unsupported MMOP %d\n", (int)op);
		return;
	}
}

static void
emit_block(FILE *f, MBlkM *b)
{
	fprintf(f, ".L%s.bb%u:\n", g_fname, b->id);
	for (uint32_t i = 0; i < b->nins; i++)
		emit_ins(f, &b->ins[i]);

	switch (b->term.op) {
	case MMOP_JMP:
		fprintf(f, "\tjmp\t.L%s.bb%u\n", g_fname, b->s1 ? b->s1->id : 0);
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
		/* return value move: eax (int) / xmm0 (float) */
		if (t.src[0] && (t.src[0]->type == MT_F32 || t.src[0]->type == MT_F64)) {
			MVal *v = t.src[0];
			fload_scratch(f, v);
		} else if (t.src[0]) {
			mv_to_scratch(f, t.src[0], "eax");
		}
		/* epilogue: restore frame and pop */
		fputs("\tmovl\t%ebp, %esp\n", f);
		fputs("\tpopl\t%ebp\n", f);
		fputs("\tret\n", f);
		break;
	}
	default:
		break;
	}
}

void
mfnm_emit_i386(MFnM *fm, FILE *f)
{
	g_mt = fm->mt;
	g_fm = fm;
	g_fname = fm->name ? fm->name : "?";

	/* frame: push ebp (4 bytes) above the slot area */
	int pushbytes = 4;
	g_slot_base = -pushbytes;

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

	int framesize = fm->slot + alloca_total(fm) + pushbytes;
	framesize = (framesize + 15) & ~15;
	g_alloca_cur = -(fm->slot + pushbytes);

	fprintf(f, ".text\n");
	if (fm->name) {
		if (fm->host && fm->host->export)
			fprintf(f, ".globl %s\n", fm->name);
		fprintf(f, "%s:\n", fm->name);
	}
	fprintf(f, "\tpushl\t%%ebp\n");
	fprintf(f, "\tmovl\t%%esp, %%ebp\n");
	if (framesize > pushbytes)
		fprintf(f, "\tsubl\t$%d, %%esp\n", framesize - pushbytes);

	/* Branch to the real entry block after the prologue */
	if (fm->start)
		fprintf(f, "\tjmp\t.L%s.bb%u\n", g_fname, fm->start->id);

	for (MBlkM *b = fm->link; b; b = b->link)
		emit_block(f, b);
}