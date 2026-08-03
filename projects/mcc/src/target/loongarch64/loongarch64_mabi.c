/* loongarch64_mabi.c — loongarch64 ABI lowering (MIR-native, LP64D scalar core).
 *
 * Lowers the pre-ABI machine IR for scalar integer/float functions:
 *   - selpar: move entry a0-a7/fa0-fa7 arguments into slots (overflow
 *     arguments are loaded from the caller-pushed stack area at fp+off);
 *   - selcall: move call arguments into a0-a7/fa0-fa7 (overflow onto the
 *     stack below sp);
 *   - selret: move the return value into a0 / fa0.
 *
 * Aggregates, varargs, floats-ops, TLS and VLA functions are rejected by
 * the loongarch64 isel (mbe_supported), so this scalar core never sees them.
 */
#include <stdlib.h>

#include "mir.h"
#include "loongarch64_m.h"

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

/* Next loongarch64 argument register of the given class (a0-a7 / fa0-fa7);
 * NULL when exhausted. */
static MVal *
rarg(MFnM *fm, int *ni, int *ns, bool isf)
{
	const MTargetM *mt = fm->mt;
	int pos = isf ? *ns : *ni;
	if (isf) (*ns)++; else (*ni)++;
	if (pos >= 8)
		return 0;
	int base = isf ? 8 : 0;   /* argreg[0..7]=a0-a7, [8..15]=fa0-fa7 */
	return reg(fm, mt->argreg[base + pos]);
}

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
	int ni = 0, ns = 0;
	int off = 0;   /* caller-pushed stack args sit at fp+off (fp = old sp) */

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, LA64MREG_FP), 0, 1, off), 0);
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
	(void)call;
	/* count stack-passed (overflow) arguments first */
	int ni = 0, ns = 0, stk = 0;
	for (int i = 0; i < n; i++) {
		bool isf = args[i].dtype == MT_F32 || args[i].dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8)
			stk += 8;
		else
			{ if (isf) ns++; else ni++; }
	}
	stk = (stk + 15) & ~15;   /* 16-align the overflow area */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, LA64MREG_SP),
		         imm(fm, MT_I64, stk));

	ni = 0; ns = 0;
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, LA64MREG_SP), 0, 1, soff),
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
	mout(o, MMOP_MOV, s0->type, reg(fm, isf ? LA64MREG_F0 : LA64MREG_A0),
	     s0, 0);
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
mfnm_abi_loongarch64(MFnM *fm)
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
