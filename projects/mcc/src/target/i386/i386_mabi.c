/* i386_mabi.c — i386 cdecl ABI lowering (MIR-native, scalar + i64 core).
 *
 * Lowers the pre-ABI machine IR for scalar + i64 functions:
 *   - selpar: load arguments from the caller-pushed stack area at
 *     ebp+8 (i32: 4-byte slots; i64: 8-byte slots);
 *   - selcall: push call arguments onto the stack (right-to-left,
 *     cdecl), then emit the call instruction;
 *   - selret: move the return value into EAX/xmm0 (i32/FP) or
 *     EAX:EDX pair (i64).
 *
 * i386 cdecl ABI: all arguments on the stack.  No argument registers.
 * The prologue pushes {ebp} (4 bytes), so the frame pointer is
 * old_esp - 4 and caller-pushed stack arguments start at ebp+8.
 *
 * Aggregates, varargs, TLS and VLA functions are rejected by the i386
 * isel (mbe_supported), so this scalar + i64 core never sees them.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "i386_m.h"

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

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
	int off = 8;   /* caller-pushed stack args start at ebp+8 (the
	                * prologue pushed {ebp}, so ebp = old_esp - 4) */

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		/* Load from stack: [ebp + off] */
		if (p->dtype == MT_I64) {
			/* i64: 8-byte stack slot, loaded into spill slot
			 * (the register allocator forces i64 to slot-resident).
			 * Load low 32 bits at off, high 32 bits at off+4. */
			int lo = dst->slot;
			mout_addr(o, MMOP_LOAD, MT_I32,
			          tmp(fm, MT_I32, "i64lo"),
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			MVal *lov = o->ins[o->nins - 1].dst;
			lov->slot = lo;
			lov->reg = -1;
			mout_addr(o, MMOP_LOAD, MT_I32,
			          tmp(fm, MT_I32, "i64hi"),
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off + 4), 0);
			MVal *hiv = o->ins[o->nins - 1].dst;
			hiv->slot = lo + 4;
			hiv->reg = -1;
			off += 8;   /* i64 takes 8 bytes on stack */
		} else {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, I386MREG_EBP), 0, 1, off), 0);
			off += 4;   /* each scalar stack slot is 4 bytes */
		}
	}
	*vafa = 0;   /* no varargs in the scalar core */
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	(void)call;
	/* Count stack bytes needed (cdecl: all args on stack; i32=4, i64=8) */
	int stk = 0;
	for (int i = 0; i < n; i++)
		stk += args[i].dtype == MT_I64 ? 8 : 4;
	stk = (stk + 15) & ~15;   /* 16-align the stack */

	/* Reserve stack space for the outgoing arguments */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, I386MREG_ESP),
		         imm(fm, MT_I64, stk));

	/* Push arguments right-to-left (cdecl convention) */
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		/* Store the argument at [esp + soff] */
		if (a->dtype == MT_I64) {
			/* i64: 8-byte value in a spill slot.  Load low 32 bits
			 * from the slot into EAX, store to [esp+soff]; load high
			 * 32 bits from slot+4 into EDX, store to [esp+soff+4]. */
			int slot = a->src[0]->slot;
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EAX),
			          maddr(reg(fm, I386MREG_EBP), 0, 1, slot), 0);
			mout_addr(o, MMOP_STORE, MT_I32, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff),
			          reg(fm, I386MREG_EAX));
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EDX),
			          maddr(reg(fm, I386MREG_EBP), 0, 1, slot + 4), 0);
			mout_addr(o, MMOP_STORE, MT_I32, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff + 4),
			          reg(fm, I386MREG_EDX));
			soff += 8;
		} else {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, I386MREG_ESP), 0, 1, soff),
			          a->src[0]);
			soff += 4;
		}
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
		/* i64 return: EAX (low 32) + EDX (high 32).
		 * The register allocator forces i64 values to slot-resident
		 * (kl_in_reg==0), so load the two halves from the slot. */
		mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EAX),
		          maddr(reg(fm, I386MREG_EBP), 0, 1, s0->slot), 0);
		mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, I386MREG_EDX),
		          maddr(reg(fm, I386MREG_EBP), 0, 1, s0->slot + 4), 0);
	} else {
		MVal *rreg = isf ? reg(fm, I386MREG_XMM0) : reg(fm, I386MREG_EAX);
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
mfnm_abi_i386(MFnM *fm)
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