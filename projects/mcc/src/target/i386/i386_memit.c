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

/* PIC/shared-code flag, set by the driver (main.c).  Consulted for GOT/PLT
 * and initial-exec TLS emission under -fPIC. */
extern int g_pic;

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
		if (v->tls) {
			/* TLS: thread pointer (TP, %gs:0) plus the TLS offset.
			 * PIC uses the initial-exec GOT form (R_386_TLS_GOTIE);
			 * non-PIC uses local-exec (R_386_TLS_LE). */
			fprintf(f, "\tmovl\t%%gs:0, %%%s\n", rn);
			if (g_pic)
				fprintf(f, "\taddl\t%s@gotntpoff, %%%s\n", sym, rn);
			else
				fprintf(f, "\taddl\t$%s@ntpoff, %%%s\n", sym, rn);
		} else if (g_pic && v->isext) {
			/* external global: load its address via the GOT (EBX is the
			 * GOT base in PIC prologues) */
			fprintf(f, "\tmovl\t%s@GOT(%%ebx), %%%s\n", sym, rn);
		} else {
			fprintf(f, "\tmovl\t$%s, %%%s\n", sym, rn);
		}
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

/* ---- i64 (Kl) half-slot addressing --------------------------------------- */

/*
 * i386 splits a 64-bit value into a low/high 32-bit pair held at
 * `off(%ebp)` and `off+4(%ebp)`.  Every i64 code path must agree on how
 * an operand is turned into that base offset, otherwise the halves are
 * read from a different place than they were written.  Two rules:
 *
 *   1. A slot-resident temp's frame offset is ALWAYS `slot + g_slot_base`
 *      — the push area sits above the slot area, so omitting the base
 *      shifts every access by 4 (non-PIC) or 8 (PIC) bytes.
 *   2. An operand without a slot (`slot == -1`: a constant, a register-
 *      resident temp, or a global) has no half-pair to read.  It must be
 *      materialised into a reserved scratch pair first; using its `slot`
 *      verbatim yields nonsense like `-1(%ebp)` / `3(%ebp)`, the latter
 *      reaching into the caller's frame.
 *
 * Two 8-byte scratch pairs are reserved at the deepest end of the frame
 * so a binary op can normalise both of its operands at once.
 */
#define I64_SCRATCH_PAIRS 2
#define I64_SCRATCH_BYTES (I64_SCRATCH_PAIRS * 8)

static int g_i64_scratch;  /* %ebp offset of scratch pair 0 (already based) */

/* Frame offset of a slot-resident value's low half. */
static int
i64_slot(const MVal *v)
{
	return v->slot + g_slot_base;
}

static bool
i64_has_slot(const MVal *v)
{
	return v && v->kind == MV_TEMP && v->reg < 0 && v->slot != -1;
}

/*
 * Resolve an i64 operand to a frame offset whose low half is at the
 * returned offset and high half at +4.  Slot-resident temps are used in
 * place; everything else is materialised into scratch pair `nth`.
 */
static int
i64_base(FILE *f, MVal *v, int nth)
{
	int off = g_i64_scratch + nth * 8;

	if (i64_has_slot(v))
		return i64_slot(v);
	if (!v) {
		fprintf(f, "\tmovl\t$0, %d(%%ebp)\n", off);
		fprintf(f, "\tmovl\t$0, %d(%%ebp)\n", off + 4);
		return off;
	}
	if (v->kind == MV_CONST && v->con && v->con->kind == MC_INT) {
		int64_t cv = v->con->u.i;
		fprintf(f, "\tmovl\t$%d, %d(%%ebp)\n", (int32_t)cv, off);
		fprintf(f, "\tmovl\t$%d, %d(%%ebp)\n",
		        (int32_t)((uint64_t)cv >> 32), off + 4);
		return off;
	}
	/* register-resident temp or any other kind: only the low word is
	 * addressable, so sign-extend it into the pair (i64 temps that made
	 * it into a single register hold a 32-bit-representable value) */
	mv_to_scratch(f, v, "eax");
	fputs("\tcdq\n", f);
	fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", off);
	fprintf(f, "\tmovl\t%%edx, %d(%%ebp)\n", off + 4);
	return off;
}

/*
 * Resolve an i64 destination.  Returns the frame offset to write both
 * halves to, or INT_MIN when the destination is not slot-resident (a
 * register-resident i64 dest can only keep its low half, which the
 * caller handles via scratch_to_dst).
 */
#define I64_NO_SLOT (-0x40000000)

static int
i64_dst_base(MVal *d)
{
	if (i64_has_slot(d))
		return i64_slot(d);
	return I64_NO_SLOT;
}

/* Store the eax(lo)/edx(hi) pair into an i64 destination. */
static void
i64_store_pair(FILE *f, MVal *d)
{
	int off;

	if (!d)
		return;
	off = i64_dst_base(d);
	if (off == I64_NO_SLOT) {
		scratch_to_dst(f, d, "eax");
		return;
	}
	fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", off);
	fprintf(f, "\tmovl\t%%edx, %d(%%ebp)\n", off + 4);
}

/* Store one 32-bit half (sloreg = "%eax"/"%edx"/"%ecx") into the lo or hi
 * half of an i64 destination's frame slot. */
static void
i64_store_half(FILE *f, MVal *d, bool hi, const char *sloreg)
{
	int off;

	if (!d)
		return;
	off = i64_dst_base(d);
	if (off == I64_NO_SLOT) {
		/* register-resident dest: only the low half fits a register */
		if (!hi)
			scratch_to_dst(f, d, sloreg);
		return;
	}
	fprintf(f, "\tmovl\t%s, %d(%%ebp)\n", sloreg, off + (hi ? 4 : 0));
}

/* Store a scratch GPR to the lo half of an i64 destination. */
static void
scratch_to_dst_i64_lo(FILE *f, MVal *d, const char *rn)
{
	i64_store_half(f, d, false, rn);
}

/* Store a scratch GPR to the hi half of an i64 destination. */
static void
scratch_to_dst_i64_hi(FILE *f, MVal *d, const char *rn)
{
	i64_store_half(f, d, true, rn);
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
		if (base->tls) {
			/* TLS base: TP + TLS offset (see mv_to_scratch). */
			fprintf(f, "\tmovl\t%%gs:0, %%ecx\n");
			if (g_pic)
				fprintf(f, "\taddl\t%s@gotntpoff, %%ecx\n",
				        base->sym ? base->sym : "0");
			else
				fprintf(f, "\taddl\t$%s@ntpoff, %%ecx\n",
				        base->sym ? base->sym : "0");
		} else if (g_pic && base->isext) {
			fprintf(f, "\tmovl\t%s@GOT(%%ebx), %%ecx\n",
			        base->sym ? base->sym : "0");
		} else {
			fprintf(f, "\tmovl\t$%s, %%ecx\n", base->sym ? base->sym : "0");
		}
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
		if (g_pic && v->isext && !v->tls) {
			fprintf(f, "\tmovl\t%s@GOT(%%ebx), %%eax\n",
			        v->sym ? v->sym : "0");
			fprintf(f, "\tmovsd\t(%%eax), %%xmm0\n");
		} else {
			fprintf(f, "\tmovsd\t%s, %%xmm0\n", v->sym ? v->sym : "0");
		}
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

	if (a && b && (a->type == MT_I64 || b->type == MT_I64)) {
		/* i64 comparison: compare high 32 bits first; if equal, compare
		 * low 32 bits with unsigned comparison.  Use a global counter
		 * for unique labels across the whole assembly file. */
		static uint32_t g_i64cc_id;
		int sa = i64_base(f, a, 0);
		int sb = i64_base(f, b, 1);
		uint32_t id = g_i64cc_id++;
		const char *suffix = i386_cc_suffix(cc);

		/* Map signed CC -> unsigned CC for low 32-bit comparison */
		const char *lsfx;
		switch (cc) {
		case MCC_EQ:  lsfx = "e";  break;
		case MCC_NE:  lsfx = "ne"; break;
		case MCC_GE:  lsfx = "ae"; break;
		case MCC_LT:  lsfx = "b";  break;
		case MCC_GT:  lsfx = "a";  break;
		case MCC_LE:  lsfx = "be"; break;
		case MCC_CS:  lsfx = "ae"; break;
		case MCC_CC:  lsfx = "b";  break;
		case MCC_HI:  lsfx = "a";  break;
		case MCC_LS:  lsfx = "be"; break;
		default:      lsfx = "e";  break;
		}

		fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sa + 4);
		fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sb + 4);
		fputs("\tcmpl\t%ecx, %eax\n", f);

		if (cc == MCC_EQ) {
			fprintf(f, "\tjne\t.Li64ne%u\n", id);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sa);
			fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sb);
			fputs("\tcmpl\t%ecx, %eax\n", f);
			fputs("\tsete\t%al\n", f);
			/* zero-extend the low byte so the result is a clean 0/1:
			 * sete only sets AL, leaving high 24 bits stale from the
			 * preceding `movl a.lo,%eax` (defect #16: comparisons
			 * produced a garbage i32 truth value). */
			fputs("\tmovzbl\t%al, %eax\n", f);
			fprintf(f, "\tjmp\t.Li64d%u\n.Li64ne%u:\n"
			        "\txorl\t%%eax, %%eax\n.Li64d%u:\n", id, id, id);
		} else if (cc == MCC_NE) {
			fprintf(f, "\tjne\t.Li64t%u\n", id);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sa);
			fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sb);
			fputs("\tcmpl\t%ecx, %eax\n", f);
			fputs("\tsetne\t%al\n", f);
			fputs("\tmovzbl\t%al, %eax\n", f);
			fprintf(f, "\tjmp\t.Li64d%u\n.Li64t%u:\n"
			        "\tmovl\t$1, %%eax\n.Li64d%u:\n", id, id, id);
		} else {
			fprintf(f, "\tjne\t.Li64hd%u\n", id);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sa);
			fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sb);
			fputs("\tcmpl\t%ecx, %eax\n", f);
			fprintf(f, "\tset%s\t%%al\n", lsfx);
			fputs("\tmovzbl\t%al, %eax\n", f);
			fprintf(f, "\tjmp\t.Li64d%u\n.Li64hd%u:\n", id, id);
			fprintf(f, "\tset%s\t%%al\n", suffix);
			fputs("\tmovzbl\t%al, %eax\n", f);
			fprintf(f, ".Li64d%u:\n", id);
		}
		scratch_to_dst(f, in->dst, "eax");
		return;
	}

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
	if (dt == MT_I64) {
		/* i64 load: two 32-bit loads from addr and addr+4 */
		emit_addr_str(f, a, addr_buf, sizeof addr_buf);
		fprintf(f, "\tmovl\t%s, %%eax\n", addr_buf);
		if (d && d->kind == MV_TEMP) {
			int dbase = i64_dst_base(d);
			if (dbase != I64_NO_SLOT) {
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", dbase);
				/* high 32 bits: addr + 4 */
				char addr2[64];
				/* Build addr+4: append +4 to the offset or use offset+4 */
				snprintf(addr2, sizeof addr2, "%d+%s",
				         (int)(a.off + 4), /* actual offset + 4 */
				         a.base ? "" : "");
				/* Re-emit the address string with offset+4 */
				if (a.base && a.base->kind == MV_REG) {
					const char *rn = mreg_name(g_mt, a.base->reg);
					snprintf(addr2, sizeof addr2, "%d(%%%s)",
					         (int)(a.off + 4), rn ? rn : "ebp");
				}
				fprintf(f, "\tmovl\t%s, %%eax\n", addr2);
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", dbase + 4);
				return;
			}
			scratch_to_dst(f, d, "eax");
		}
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
	if (dt == MT_I64) {
		/* i64 store: two 32-bit stores to addr and addr+4, with the source
		 * normalised into an addressable half-pair first (i64_base) so a
		 * constant or register-resident source stores its real halves. */
		int sbase = i64_base(f, s0, 0);
		emit_addr_str(f, a, addr_buf, sizeof addr_buf);
		fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase);
		fprintf(f, "\tmovl\t%%eax, %s\n", addr_buf);
		/* high 32 bits: build addr+4 */
		char addr2[64];
		if (a.base && a.base->kind == MV_REG) {
			const char *rn = mreg_name(g_mt, a.base->reg);
			snprintf(addr2, sizeof addr2, "%d(%%%s)",
			         (int)(a.off + 4), rn ? rn : "ebp");
		} else {
			snprintf(addr2, sizeof addr2, "%lld",
			         (long long)(a.off + 4));
		}
		fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase + 4);
		fprintf(f, "\tmovl\t%%eax, %s\n", addr2);
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
		if (in->dtype == MT_I64 || (s0 && s0->type == MT_I64)) {
			/* i64 on i386: two 32-bit moves, normalised through a single
			 * half-pair base so the source halves are read back from the
			 * frame address they were actually written to (i64_base).
			 * Move low 32 bits first, then high 32 bits at base+4. */
			int sslot = i64_base(f, s0, 0);
			int dbase = i64_dst_base(d);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot);
			if (dbase != I64_NO_SLOT)
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", dbase);
			else
				scratch_to_dst(f, d, "eax");
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot + 4);
			if (dbase != I64_NO_SLOT)
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", dbase + 4);
			return;
		}
		mv_to_scratch(f, s0, "eax");
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_MOVSX:
		if (in->dtype == MT_I64) {
			/* Widening a 32-bit source to 64 bits: lo' = value, hi' = sign.
			 * A constant source must materialise BOTH halves; mv_to_scratch
			 * only loads the low word (i386 has no 32->64 extension to a
			 * pair here), so a runtime source is sign-extended with cdq. */
			if (s0 && s0->kind == MV_CONST && s0->con) {
				int64_t cv = s0->con->u.i;
				fprintf(f, "\tmovl\t$%d, %%eax\n", (int32_t)cv);
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", d->slot + g_slot_base);
				fprintf(f, "\tmovl\t$%d, %%eax\n", (int32_t)((uint64_t)cv >> 32));
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", d->slot + 4 + g_slot_base);
			} else {
				mv_to_scratch(f, s0, "eax");
				fputs("\tcdq\n", f);         /* sign-extend eax -> edx */
				scratch_to_dst(f, d, "eax");
				int off = d->slot + 4 + g_slot_base;
				fprintf(f, "\tmovl\t%%edx, %d(%%ebp)\n", off);
			}
			return;
		}
		mv_to_scratch(f, s0, "eax");
		switch (in->dtype) {
		case MT_I8:  fputs("\tmovsbl\t%al, %eax\n", f); break;
		case MT_I16: fputs("\tmovswl\t%ax, %eax\n", f); break;
		default:     break;
		}
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_MOVZX:
		if (in->dtype == MT_I64) {
			/* Zero-extension to 64: lo' = value, hi' = 0 */
			if (s0 && s0->kind == MV_CONST && s0->con) {
				int64_t cv = s0->con->u.i;
				fprintf(f, "\tmovl\t$%d, %%eax\n", (int32_t)cv);
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", d->slot + g_slot_base);
				fprintf(f, "\tmovl\t$0, %%eax\n");
				fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", d->slot + 4 + g_slot_base);
			} else {
				mv_to_scratch(f, s0, "eax");
				scratch_to_dst(f, d, "eax");
				int off = d->slot + 4 + g_slot_base;
				fprintf(f, "\tmovl\t$0, %d(%%ebp)\n", off);
			}
			return;
		}
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
		/* i64 on i386: split into low/high with carry/borrow */
		if (in->dtype == MT_I64) {
			/* Every arm below re-reads the operand halves from the
			 * frame, so both operands are normalised into addressable
			 * lo/hi pairs and the destination is written through
			 * i64_store_pair (see i64_base). */
			int sslot0 = i64_base(f, s0, 0);
			int sslot1 = i64_base(f, s1, 1);
			if (op == MMOP_ADD || op == MMOP_SUB) {
				/* low: addl/subl into eax; high: adcl/sbbl into edx.
				 * The high half is computed while the carry/borrow from
				 * the low half is still live, so nothing may sit between
				 * the two arithmetic instructions that touches EFLAGS —
				 * hence edx is loaded before the low op runs. */
				bool isadd = (op == MMOP_ADD);
				fprintf(f, "\tmovl\t%d(%%ebp), %%edx\n", sslot0 + 4);
				fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0);
				fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);
				fprintf(f, "\t%s\t%%ecx, %%eax\n", isadd ? "addl" : "subl");
				fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1 + 4);
				fprintf(f, "\t%s\t%%ecx, %%edx\n", isadd ? "adcl" : "sbbl");
				i64_store_pair(f, d);
			} else if (op == MMOP_SHL || op == MMOP_SHR || op == MMOP_SAR) {
				/* i64 shift on i386: the 64-bit value lives in two 32-bit
				 * halves (lo @ sslot0, hi @ sslot0+4) and a correct shift must
				 * move bits across the halves, unlike the naive per-half
				 * fallback (which loses the carried bits for shifts >= 32 —
				 * `1LL << 40` would zero the high half instead of moving the
				 * bit up).  i386 register shifts take the count in %cl (8-bit)
				 * — never the whole %ecx — and we avoid shld/shrd (mt/as only
				 * knows plain shl/shr/sar), so carry bits are combined by
				 * re-reading operand halves from their slots.  Constant shift
				 * counts/operands are normalised into their slots first so the
				 * slot reads below see fully-stored values. */
				static uint32_t g_i64sh_id;
				uint32_t sid = g_i64sh_id++;
				const char *sh   = (op == MMOP_SHL) ? "shl"   :
				                  (op == MMOP_SAR) ? "sar"   : "shr";
				bool is_arith = (op == MMOP_SAR);
				/* Normalise both operands into addressable lo/hi pairs: the
				 * code below re-reads them from the frame across the two
				 * arms, so a constant or register-resident operand must be
				 * spilled to scratch first (see i64_base). */
				sslot0 = i64_base(f, s0, 0);
				sslot1 = i64_base(f, s1, 1);
				/* load lo/hi/s; check s < 32 */
				fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0);
				fprintf(f, "\tmovl\t%d(%%ebp), %%edx\n", sslot0 + 4);
				fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);
				fprintf(f, "\tcmpl\t$32, %%ecx\n");
				fprintf(f, "\tjge\t.Li64sh_ge%u\n", sid);
				if (op == MMOP_SHL) {
					/* s < 32: lo' = lo<<s ; hi' = (hi<<s)|(lo>>(32-s)).
					 * ebx is scratch (i386 MIR is slot-resident here).  The
					 * shift count is always reloaded as %cl. */
					fprintf(f, "\tmovl\t%%eax, %%ebx\n");   /* ebx = lo */
					fprintf(f, "\tshl\t%%cl, %%eax\n");     /* eax = lo' */
					fprintf(f, "\tnegl\t%%ecx\n");
					fprintf(f, "\taddl\t$32, %%ecx\n");     /* cl = 32-s */
					fprintf(f, "\tshr\t%%cl, %%ebx\n");     /* ebx = lo>>(32-s) = carry */
					fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);  /* cl = s */
					fprintf(f, "\tmovl\t%d(%%ebp), %%edx\n", sslot0 + 4);  /* edx = hi */
					fprintf(f, "\tshl\t%%cl, %%edx\n");     /* edx = hi<<s */
					fprintf(f, "\torl\t%%ebx, %%edx\n");    /* edx = hi' */
				} else if (op == MMOP_SHR) {
					/* s < 32 (logical): hi' = hi>>s ; lo' = (lo>>s)|(hi<<(32-s)) */
					fprintf(f, "\tmovl\t%%edx, %%ebx\n");   /* ebx = hi */
					fprintf(f, "\tshr\t%%cl, %%ebx\n");     /* ebx = hi' (saved) */
					fprintf(f, "\tnegl\t%%ecx\n");
					fprintf(f, "\taddl\t$32, %%ecx\n");     /* cl = 32-s */
					fprintf(f, "\tshl\t%%cl, %%edx\n");     /* edx = hi<<(32-s) */
					fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0);  /* eax = lo */
					fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);  /* cl = s */
					fprintf(f, "\tshr\t%%cl, %%eax\n");     /* eax = lo>>s */
					fprintf(f, "\torl\t%%edx, %%eax\n");    /* eax = lo' */
					fprintf(f, "\tmovl\t%%ebx, %%edx\n");   /* edx = hi' */
				} else {
					/* SAR s < 32 (arithmetic): hi' = hi>>s (sign fill) ;
					 * lo' = (lo>>s)|(hi<<(32-s)) */
					fprintf(f, "\tmovl\t%%edx, %%ebx\n");   /* ebx = hi */
					fprintf(f, "\tsar\t%%cl, %%ebx\n");     /* ebx = hi' (saved) */
					fprintf(f, "\tnegl\t%%ecx\n");
					fprintf(f, "\taddl\t$32, %%ecx\n");     /* cl = 32-s */
					fprintf(f, "\tshl\t%%cl, %%edx\n");     /* edx = hi<<(32-s) */
					fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0);  /* eax = lo */
					fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);  /* cl = s */
					fprintf(f, "\tsar\t%%cl, %%eax\n");     /* eax = lo>>s */
					fprintf(f, "\torl\t%%edx, %%eax\n");    /* eax = lo' */
					fprintf(f, "\tmovl\t%%ebx, %%edx\n");   /* edx = hi' */
				}
				fprintf(f, "\tjmp\t.Li64sh_done%u\n", sid);
				/* ---- s >= 32 ---- */
				fprintf(f, ".Li64sh_ge%u:\n", sid);
				if (op == MMOP_SHL) {
					/* hi' = lo<<(s-32); lo' = 0 */
					fprintf(f, "\tsubl\t$32, %%ecx\n");
					fprintf(f, "\tmovl\t%%eax, %%edx\n");    /* edx = lo */
					fprintf(f, "\tshl\t%%cl, %%edx\n");      /* edx = lo<<(s-32) */
					fprintf(f, "\txorl\t%%eax, %%eax\n");    /* lo' = 0 */
				} else {
					/* SHR/SAR s >= 32: lo' = hi>>(s-32); hi' = 0 or sign fill */
					fprintf(f, "\tsubl\t$32, %%ecx\n");
					fprintf(f, "\tmovl\t%%edx, %%eax\n");    /* eax = hi */
					fprintf(f, "\t%s\t%%cl, %%eax\n", sh);   /* eax = hi>>(s-32) = lo' */
					if (is_arith)
						fprintf(f, "\tsar\t$31, %%edx\n");  /* hi' = sign fill */
					else
						fprintf(f, "\txorl\t%%edx, %%edx\n"); /* hi' = 0 */
				}
				fprintf(f, ".Li64sh_done%u:\n", sid);
				i64_store_pair(f, d);
				return;
			} else {
				/* fallback: load low, do 32-bit op into eax and save;
				 * then load high, do 32-bit op into edx (no carry) */
				const char *opn = binop_name(op);
				fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0);
				fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1);
				fprintf(f, "\t%s\t%%ecx, %%eax\n", opn);
				fputs("\tpushl\t%eax\n", f);
				fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sslot0 + 4);
				fprintf(f, "\tmovl\t%d(%%ebp), %%ecx\n", sslot1 + 4);
				fprintf(f, "\t%s\t%%ecx, %%eax\n", opn);
				fputs("\tmovl\t%eax, %edx\n", f);
				fputs("\tpopl\t%eax\n", f);
				i64_store_pair(f, d);
			}
			return;
		}
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
		if (in->dtype == MT_I64) {
			/* i64 negation: low = 0 - low (sets CF), high = 0 - high - CF.
			 * Use i64_base/i64_store_pair so the slot addresses carry
			 * g_slot_base and match what producers write (raw d->slot here
			 * drifted by the push-area size). */
			int sbase = i64_base(f, s0, 0);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase);
			fputs("\tnegl\t%eax\n", f);
			fputs("\tmovl\t%eax, %edx\n", f);   /* stash lo result */
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase + 4);
			fputs("\tsbbl\t$0, %eax\n", f);
			fputs("\tmovl\t%eax, %ecx\n", f);   /* hi result in ecx; the
			                                      * sbbl consumed the CF from
			                                      * the negl above */
			scratch_to_dst_i64_lo(f, d, "edx");
			scratch_to_dst_i64_hi(f, d, "ecx");
			return;
		}
		mv_to_scratch(f, s0, "eax");
		fputs("\tnegl\t%eax\n", f);
		scratch_to_dst(f, d, "eax");
		return;
	case MMOP_NOT:
		if (in->dtype == MT_I64) {
			/* i64 bitwise NOT: NOT both halves */
			int sbase = i64_base(f, s0, 0);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase);
			fputs("\tnotl\t%eax\n", f);
			scratch_to_dst_i64_lo(f, d, "eax");
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", sbase + 4);
			fputs("\tnotl\t%eax\n", f);
			scratch_to_dst_i64_hi(f, d, "eax");
			return;
		}
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
		static uint32_t g_u2f_id;
		bool is32 = (op == MMOP_CVTSI2SS_U);
		uint32_t id = g_u2f_id++;
		mv_to_scratch(f, s0, "eax");
		/* unsigned conversion: test if sign bit is set, handle via
		 * float conversion trick */
		fprintf(f, "\ttestl\t%%eax, %%eax\n");
		fprintf(f, "\tjs\t.Lu2f%u\n", id);
		fprintf(f, "\tcvtsi2%s\t%%eax, %%xmm0\n", is32 ? "ss" : "sd");
		fprintf(f, "\tjmp\t.Lu2fe%u\n", id);
		fprintf(f, ".Lu2f%u:\n", id);
		/* push %eax, then convert via memory */
		fputs("\tpushl\t%eax\n", f);
		fputs("\tfildl\t(%esp)\n", f);
		fputs("\taddl\t$4, %esp\n", f);
		if (is32)
			fputs("\tfstps\t-4(%esp)\n\tmovss\t-4(%esp), %xmm0\n", f);
		else
			fputs("\tfstpl\t-8(%esp)\n\tmovsd\t-8(%esp), %xmm0\n", f);
		fprintf(f, ".Lu2fe%u:\n", id);
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
			fprintf(f, "\tcall\t%s%s\n", s0->sym ? s0->sym : "?",
			        g_pic && s0->isext ? "@plt" : "");
		} else if (s0) {
			mv_to_scratch(f, s0, "eax");
			fprintf(f, "\tcall\t*%%eax\n");
		}
		if (d && !in->td) {
			if (d->type == MT_F32 || d->type == MT_F64) {
				fstore_scratch(f, d);
			} else if (d->type == MT_I64) {
				/* i64 return: EDX:EAX -> store both halves to the value's
				 * frame slot.  Use i64_dst_base so the offset matches what
				 * every consumer reads via i64_base (slot + g_slot_base);
				 * writing to the raw d->slot here leaves consumers reading
				 * slot+g_slot_base — off by the push-area size (i64 call
				 * result compare skew, defect #16). */
				int dbase = i64_dst_base(d);
				if (dbase != I64_NO_SLOT) {
					fprintf(f, "\tmovl\t%%eax, %d(%%ebp)\n", dbase);
					fprintf(f, "\tmovl\t%%edx, %d(%%ebp)\n", dbase + 4);
				} else {
					/* register-resident dest: only the low word fits a
					 * single register (i64 with a stray non-slot dest) */
					scratch_to_dst(f, d, "eax");
				}
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
	case MMOP_BLIT: {
		/* aggregate copy: src[1] -> src[0], cst bytes.
		 * Use EAX as the data register, ECX/EDX for addresses.
		 * Copy in 4/2/1 byte chunks. */
		MConst *bc = in->cst;
		int64_t sz = bc ? bc->u.i : 0;
		MVal *dp = s0, *sp = s1;
		/* Load destination address into ECX */
		if (dp && dp->kind == MV_REG)
			fprintf(f, "\tmovl\t%%%s, %%ecx\n", mreg_name(g_mt, dp->reg));
		else {
			mv_to_scratch(f, dp, "ecx");
		}
		/* Load source address into EDX */
		if (sp && sp->kind == MV_REG)
			fprintf(f, "\tmovl\t%%%s, %%edx\n", mreg_name(g_mt, sp->reg));
		else {
			mv_to_scratch(f, sp, "edx");
		}
		int64_t off = 0;
		while (sz >= 4) {
			fprintf(f, "\tmovl\t%lld(%%edx), %%eax\n", (long long)off);
			fprintf(f, "\tmovl\t%%eax, %lld(%%ecx)\n", (long long)off);
			off += 4;
			sz -= 4;
		}
		if (sz >= 2) {
			fprintf(f, "\tmovw\t%lld(%%edx), %%ax\n", (long long)off);
			fprintf(f, "\tmovw\t%%ax, %lld(%%ecx)\n", (long long)off);
			off += 2;
			sz -= 2;
		}
		if (sz >= 1) {
			fprintf(f, "\tmovb\t%lld(%%edx), %%al\n", (long long)off);
			fprintf(f, "\tmovb\t%%al, %lld(%%ecx)\n", (long long)off);
		}
		return;
	}
	case MMOP_CMP: {
		/* compare src[0] vs src[1], set flags */
		mv_to_scratch(f, s0, "eax");
		mv_to_scratch(f, s1, "ecx");
		fputs("\tcmpl\t%ecx, %eax\n", f);
		return;
	}
	case MMOP_SETCC: {
		/* dst = flags.cc ? 1 : 0 */
		fprintf(f, "\tset%s\t%%al\n", i386_cc_suffix(in->cc));
		fputs("\tmovzbl\t%al, %eax\n", f);
		scratch_to_dst(f, d, "eax");
		return;
	}
	case MMOP_VASTART: {
		/* va_start(ap): store the address of the first vararg into ap.
		 * The address is computed from vafa (stored in the MIR function's
		 * vafa field by the ABI lowering).  The terminator's vafa is
		 * stored in term.td or in the function's vafa — but we need a
		 * way to get it here.  The ABI lowering replaces MMOP_VASTART
		 * with concrete instructions, so this should never be reached
		 * in the emitter.  For safety, emit a no-op. */
		fprintf(f, "\t# vastart\n");
		return;
	}
	case MMOP_VAARG: {
		/* va_arg(ap, type): load the next argument.  The ABI lowering
		 * replaces MMOP_VAARG with concrete instructions, so this
		 * should never be reached. */
		fprintf(f, "\t# vaarg\n");
		return;
	}
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
		/* return value move: eax (int) / edx:eax (i64) / xmm0 (float) */
		if (t.src[0] && t.src[0]->type == MT_I64) {
			/* i64: lo -> EAX, hi -> EDX.  i64_base resolves the value's
			 * halves regardless of whether it is a constant, register, or
			 * slot-resident temp. */
			int base = i64_base(f, t.src[0], 0);
			fprintf(f, "\tmovl\t%d(%%ebp), %%eax\n", base);
			fprintf(f, "\tmovl\t%d(%%ebp), %%edx\n", base + 4);
		} else if (t.src[0] && (t.src[0]->type == MT_F32 || t.src[0]->type == MT_F64)) {
			MVal *v = t.src[0];
			fload_scratch(f, v);
		} else if (t.src[0]) {
			mv_to_scratch(f, t.src[0], "eax");
		}
		/* epilogue: restore frame and pop */
		fputs("\tmovl\t%ebp, %esp\n", f);
		if (g_pic)
			fputs("\tpopl\t%ebx\n", f);
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

	/* frame: push ebp (4 bytes) above the slot area; PIC additionally
	 * pushes the callee-saved EBX (GOT base) before setting ebp. */
	int pushbytes = 4;
	if (g_pic)
		pushbytes += 4;
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

	/* Reserve the i64 half-pair scratch below the register-allocated
	 * slots (see i64_base): normalising a constant or register-resident
	 * i64 operand needs a real lo/hi pair to read back from. */
	g_i64_scratch = -(fm->slot + pushbytes + I64_SCRATCH_BYTES);

	int framesize = fm->slot + I64_SCRATCH_BYTES + alloca_total(fm) + pushbytes;
	framesize = (framesize + 15) & ~15;
	g_alloca_cur = -(fm->slot + pushbytes + I64_SCRATCH_BYTES);

	fprintf(f, ".text\n");
	if (fm->name) {
		if (fm->host && fm->host->export)
			fprintf(f, ".globl %s\n", fm->name);
		fprintf(f, "%s:\n", fm->name);
	}
	fprintf(f, "\tpushl\t%%ebp\n");
	if (g_pic)
		fprintf(f, "\tpushl\t%%ebx\n");
	fprintf(f, "\tmovl\t%%esp, %%ebp\n");
	if (framesize > pushbytes)
		fprintf(f, "\tsubl\t$%d, %%esp\n", framesize - pushbytes);

	if (g_pic) {
		/* Load the GOT base into EBX for PIC direct-global addressing. */
		fprintf(f, "\tcall\t__x86.get_pc_thunk.bx\n");
		fprintf(f, "\taddl\t$_GLOBAL_OFFSET_TABLE_, %%ebx\n");
	}

	/* Branch to the real entry block after the prologue */
	if (fm->start)
		fprintf(f, "\tjmp\t.L%s.bb%u\n", g_fname, fm->start->id);

	for (MBlkM *b = fm->link; b; b = b->link)
		emit_block(f, b);
}