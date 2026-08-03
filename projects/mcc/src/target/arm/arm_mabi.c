/* arm_mabi.c — ARM ABI lowering (MIR-native, AAPCS32 scalar core).
 *
 * Lowers the pre-ABI machine IR for scalar integer functions:
 *   - selpar: move entry r0-r3/d0-d7 arguments into slots (overflow
 *     arguments are loaded from the caller-pushed stack area at fp+8);
 *   - selcall: move call arguments into r0-r3/d0-d7 (overflow onto the
 *     stack below sp);
 *   - selret: move the return value into r0 / d0.
 *
 * AAPCS32 difference vs riscv64/aarch64: only 4 GPR argument registers
 * (r0-r3).  The prologue pushes {r11, lr} (8 bytes), so the frame
 * pointer is old_sp - 8 and caller-pushed stack arguments start at fp+8.
 *
 * Aggregates, varargs, TLS and VLA functions are rejected by the ARM
 * isel (mbe_supported), so this scalar core never sees them.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "arm_m.h"

/* ---- output stream (block rewrite) ------------------------------------- */

typedef struct MOut {
	MBlkM *b;
	MInsM *ins;
	uint32_t nins, cins;
} MOut;

static void
mout_end(MOut *o)
{
	free(o->b->ins);
	o->b->ins = o->ins;
	o->b->nins = o->nins;
	o->b->cins = o->cins;
}

static MInsM *
mout_alloc(MOut *o)
{
	if (o->nins == o->cins) {
		o->cins = o->cins ? o->cins * 2 : 16;
		o->ins = realloc(o->ins, o->cins * sizeof *o->ins);
	}
	MInsM *in = &o->ins[o->nins++];
	memset(in, 0, sizeof *in);
	in->id = o->nins - 1;
	in->blk = o->b;
	return in;
}

static MInsM *
mout(MOut *o, MMOP op, MType dt, MVal *dst, MVal *s0, MVal *s1)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->src[0] = s0;
	in->src[1] = s1;
	return in;
}

static MInsM *
mout_addr(MOut *o, MMOP op, MType dt, MVal *dst, MAddr a, MVal *s0)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->addr = a;
	in->src[0] = s0;
	return in;
}

static MInsM *
mout_cst(MOut *o, MMOP op, MType dt, MVal *dst, MVal *s0, MConst *c)
{
	MInsM *in = mout_alloc(o);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->src[0] = s0;
	in->cst = c;
	return in;
}

/* ---- helpers ------------------------------------------------------------ */

static MVal *
reg(MFnM *fm, int r)
{
	return mfn_reg(fm->host, fm->mt, r);
}

static MVal *
tmp(MFnM *fm, MType t, const char *tag)
{
	return mval_new(fm->host, MV_TEMP, t, 0, tag);
}

static MConst *
imm(MFnM *fm, MType t, int64_t v)
{
	return mconst_int(fm->host, t, v);
}

/* Next ARM argument register of the given class (r0-r3 / d0-d7);
 * NULL when exhausted. */
static MVal *
rarg(MFnM *fm, int *ni, int *ns, bool isf)
{
	const MTargetM *mt = fm->mt;
	int pos = isf ? *ns : *ni;
	int lim = isf ? 8 : 4;   /* 8 FPRs (d0-d7), 4 GPRs (r0-r3) */
	if (isf) (*ns)++; else (*ni)++;
	if (pos >= lim)
		return 0;
	int base = isf ? 4 : 0;   /* argreg[0..3]=r0-r3, [4..11]=d0-d7 */
	return reg(fm, mt->argreg[base + pos]);
}

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
	const MTargetM *mt = fm->mt;
	int ni = 0, ns = 0;
	int off = 8;   /* caller-pushed stack args sit at fp+8 (the prologue
	                * pushed {r11, lr}, so fp = old_sp - 8) */

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;
		if (p->dtype == MT_I64) {
			/* AAPCS: i64 needs even-aligned register pair.
			 * If ni is odd, skip one register. */
			if (ni < 4 && (ni & 1))
				ni++;
			int slot = dst->slot;
			if (ni + 1 < 4) {
				/* In registers r0:r1 or r2:r3 */
				int lo_reg = mt->argreg[0 + ni];
				int hi_reg = mt->argreg[0 + ni + 1];
				ni += 2;
				mout(o, MMOP_MOV, MT_I32, tmp(fm, MT_I32, "i64lo"),
				     reg(fm, lo_reg), 0);
				MVal *lov = o->ins[o->nins - 1].dst;
				lov->slot = slot;
				lov->reg = -1;
				mout(o, MMOP_MOV, MT_I32, tmp(fm, MT_I32, "i64hi"),
				     reg(fm, hi_reg), 0);
				MVal *hiv = o->ins[o->nins - 1].dst;
				hiv->slot = slot + 4;
				hiv->reg = -1;
			} else {
				/* From stack */
				mout_addr(o, MMOP_LOAD, MT_I32,
				          tmp(fm, MT_I32, "i64lo"),
				          maddr(reg(fm, ARM_R11), 0, 1, off), 0);
				MVal *lov = o->ins[o->nins - 1].dst;
				lov->slot = slot;
				lov->reg = -1;
				mout_addr(o, MMOP_LOAD, MT_I32,
				          tmp(fm, MT_I32, "i64hi"),
				          maddr(reg(fm, ARM_R11), 0, 1, off + 4), 0);
				MVal *hiv = o->ins[o->nins - 1].dst;
				hiv->slot = slot + 4;
				hiv->reg = -1;
				off += 8;
			}
			continue;
		}
		if (isf ? ns >= 8 : ni >= 4) {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, ARM_R11), 0, 1, off), 0);
			off += 8;
			continue;
		}
		mout(o, MMOP_MOV, p->dtype, dst, rarg(fm, &ni, &ns, isf), 0);
	}
	*vafa = 0;   /* no varargs in the scalar core */
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	const MTargetM *mt = fm->mt;
	(void)call;
	/* count stack-passed (overflow) arguments first */
	int ni = 0, ns = 0, stk = 0;
	for (int i = 0; i < n; i++) {
		if (args[i].dtype == MT_I64) {
			/* i64: needs even-aligned register pair */
			if (ni < 4 && (ni & 1)) ni++;
			if (ni + 1 < 4)
				ni += 2;
			else
				stk += 8;
		} else {
			bool isf = args[i].dtype == MT_F32 || args[i].dtype == MT_F64;
			if (isf ? ns >= 8 : ni >= 4)
				stk += 8;
			else
				{ if (isf) ns++; else ni++; }
		}
	}
	stk = (stk + 7) & ~7;    /* 8-align the overflow area (AAPCS) */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, ARM_SP),
		         imm(fm, MT_I64, stk));

	ni = 0; ns = 0;
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->dtype == MT_I64) {
			/* AAPCS: i64 needs even-aligned register pair */
			if (ni < 4 && (ni & 1)) ni++;
			if (ni + 1 < 4) {
				/* In registers rN:rN+1 (low in rN, high in rN+1).
				 * The value is slot-resident; load low/high from slot. */
				int slot = a->src[0]->slot;
				int lo_reg = mt->argreg[0 + ni];
				int hi_reg = mt->argreg[0 + ni + 1];
				ni += 2;
				mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, lo_reg),
				          maddr(reg(fm, ARM_R11), 0, 1, slot), 0);
				mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, hi_reg),
				          maddr(reg(fm, ARM_R11), 0, 1, slot + 4), 0);
			} else {
				/* Stack overflow: store low and high */
				int slot = a->src[0]->slot;
				mout_addr(o, MMOP_LOAD, MT_I32, tmp(fm, MT_I32, "i64tmp"),
				          maddr(reg(fm, ARM_R11), 0, 1, slot), 0);
				MVal *tmpv = o->ins[o->nins - 1].dst;
				mout_addr(o, MMOP_STORE, MT_I32, 0,
				          maddr(reg(fm, ARM_SP), 0, 1, soff), tmpv);
				mout_addr(o, MMOP_LOAD, MT_I32, tmp(fm, MT_I32, "i64tmp"),
				          maddr(reg(fm, ARM_R11), 0, 1, slot + 4), 0);
				MVal *tmpv2 = o->ins[o->nins - 1].dst;
				mout_addr(o, MMOP_STORE, MT_I32, 0,
				          maddr(reg(fm, ARM_SP), 0, 1, soff + 4), tmpv2);
				soff += 8;
			}
			continue;
		}
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 4) {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, ARM_SP), 0, 1, soff),
			          a->src[0]);
			soff += 8;
			continue;
		}
		mout(o, MMOP_MOV, a->dtype, rarg(fm, &ni, &ns, isf), a->src[0], 0);
	}

	/* the call itself (dst = result; src[0] = callee) */
	mout(o, MMOP_CALL, call->dtype, call->dst, call->src[0], 0);
}

/* ---- selret: return lowering ------------------------------------------- */

static void
mabi_selret(MFnM *fm, MOut *o, MInsM *term)
{
	MVal *s0 = term->src[0];
	if (!s0) {
		term->op = MMOP_RET;
		term->src[0] = 0;
		term->td = 0;
		return;
	}
	bool isf = s0->type == MT_F32 || s0->type == MT_F64;
	if (s0->type == MT_I64) {
		/* i64 return: r0 (low 32) + r1 (high 32).
		 * The register allocator forces i64 values to slot-resident
		 * (kl_in_reg==0), so load the two halves from the slot. */
		mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, ARM_R0),
		          maddr(reg(fm, ARM_R11), 0, 1, s0->slot), 0);
		mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, ARM_R1),
		          maddr(reg(fm, ARM_R11), 0, 1, s0->slot + 4), 0);
	} else {
		MVal *rreg = isf ? reg(fm, ARM_D0) : reg(fm, ARM_R0);
		mout(o, MMOP_MOV, s0->type, rreg, s0, 0);
	}
	/* the RET terminator now returns the value already in the register */
	term->op = MMOP_RET;
	term->src[0] = 0;
	term->td = 0;
}

/* ---- block rewrite ------------------------------------------------------ */

static void
mabi_block(MFnM *fm, MBlkM *b, uint32_t start, MOut *o)
{
	MInsM *args = 0;
	uint32_t nargs = 0, cargs = 0;

	for (uint32_t k = start; k < b->nins; k++) {
		MInsM *in = &b->ins[k];
		if (in->op == MMOP_ARG) {
			if (nargs == cargs) {
				cargs = cargs ? cargs * 2 : 8;
				args = realloc(args, cargs * sizeof *args);
			}
			args[nargs++] = *in;
			continue;
		}
		if (in->op == MMOP_CALL) {
			mabi_selcall(fm, o, args, nargs, in);
			nargs = 0;
			continue;
		}
		mout(o, in->op, in->dtype, in->dst, in->src[0], in->src[1]);
		o->ins[o->nins - 1].addr = in->addr;
		o->ins[o->nins - 1].cst = in->cst;
		o->ins[o->nins - 1].cc = in->cc;
		o->ins[o->nins - 1].td = in->td;
		o->ins[o->nins - 1].extra = in->extra;
	}
	free(args);

	if (b->term.op == MMOP_RET && (b->term.src[0] || b->term.td))
		mabi_selret(fm, o, &b->term);
}

void
mfnm_abi_arm(MFnM *fm)
{
	if (fm->start) {
		MBlkM *b = fm->start;
		MOut o = {.b = b};
		uint32_t start = 0;
		while (start < b->nins && b->ins[start].op == MMOP_PARM)
			start++;
		mabi_selpar(fm, &o, b->ins, start, &fm->vafa);
		mabi_block(fm, b, start, &o);
		mout_end(&o);
	}
	for (MBlkM *b = fm->link; b; b = b->link) {
		if (b == fm->start)
			continue;
		MOut o = {.b = b};
		mabi_block(fm, b, 0, &o);
		mout_end(&o);
	}
}
