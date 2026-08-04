/* aarch64_mabi.c — aarch64 ABI lowering (MIR-native, AAPCS64).
 *
 * Lowers the pre-ABI machine IR:
 *   - selpar: entry x0-x7/v0-v7 args into slots; aggregates ≤16B split
 *     into 8-byte register chunks, >16B / incomplete passed by reference;
 *     sret (>16B return) uses the hidden buffer in x8 (mt->sret_reg).
 *   - selcall: move args into x0-x7/v0-v7 / v-reg chunks (overflow onto
 *     the stack below sp); sret passes the pad pointer in x8.
 *   - selret: move the return value into x0 / v0, or pack ≤16B aggregate
 *     chunks into x0/x1 / v0/v1; sret blits into the hidden x8 buffer.
 *   - va_start/va_arg: AAPCS64 32-byte va_list (__stack/__gr_top/__vr_top/
 *     __gr_offs/__vr_offs), branchless select like the x86_64 mabi.
 *
 * AAPCS64 stack arguments sit at fp+16 (prologue stp pushed fp+lr, so
 * fp = old_sp - 16).  The varargs reg_save_area is a 192-byte alloca
 * (8 GP x0-x7 + 8 V v0-v7, 16-byte V slots).
 */
#include <stdlib.h>

#include "mir.h"
#include "aarch64_m.h"

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

/* ---- aggregate classification (AAPCS64, simplified like riscv64) --------
 * An aggregate is passed/returned in up to two 8-byte chunks:
 *   - all-FP structs/arrays (every scalar leaf is float/double, not a
 *     union) use v0/v1 — matches the legacy fpstruct() model;
 *   - anything else ≤16B is split into 8-byte INTEGER chunks (x0/x1);
 *   - size 0 / incomplete / >16B is passed/returned by reference (sret
 *     through x8). */
typedef struct A64Class {
	MTypeDesc *td;
	uint32_t size;
	int nreg;          /* 0..2 */
	MType cls[2];      /* per-chunk scalar type (MT_I64 or MT_F64) */
	uint32_t off[2];   /* byte offset of each chunk within the aggregate */
	bool inmem;
} A64Class;

/* Recursive all-FP check: fills cls/off for FP chunks, returns the FP
 * chunk count, or -1 when the aggregate has a non-FP leaf / is a union /
 * would need a third chunk. */
static int
a64_fpstruct(MTypeDesc *td, int off, A64Class *c)
{
	if (td->is_union)
		return -1;
	if (td->is_array) {
		if (td->elem_type == MT_AGG)
			return a64_fpstruct(td->elem_desc, off, c);
		int n = c->nreg;
		if (n + (int)td->nelem > 2)
			return -1;
		for (uint64_t i = 0; i < td->nelem; i++) {
			MType et = td->elem_type;
			if (et != MT_F32 && et != MT_F64)
				return -1;
			c->cls[n] = et;
			c->off[n] = off + i * (et == MT_F64 ? 8 : 4);
			n++;
		}
		c->nreg = n;
		return n;
	}
	for (uint32_t i = 0; i < td->nfield; i++) {
		MField *f = &td->field[i];
		if (f->type == MT_AGG) {
			if (a64_fpstruct(f->sub, off + (int)f->offset, c) == -1)
				return -1;
		} else if (f->type == MT_F32 || f->type == MT_F64) {
			int n = c->nreg;
			if (n == 2)
				return -1;
			c->cls[n] = f->type;
			c->off[n] = off + (int)f->offset;
			c->nreg = n + 1;
		} else {
			return -1;   /* non-FP leaf: not an fp-struct */
		}
	}
	return c->nreg;
}

static void
a64_classify(A64Class *c, MTypeDesc *td)
{
	memset(c, 0, sizeof *c);
	c->td = td;
	uint32_t sz = td->size;
	uint32_t al = td->align < 8 ? 8 : (uint32_t)td->align;
	sz = (sz + al - 1) & ~(al - 1);
	c->size = sz;
	if (td->is_incomplete || sz > 16 || sz == 0) {
		c->inmem = 1;
		return;
	}
	if (a64_fpstruct(td, 0, c) >= 0)
		return;
	/* non-FP aggregate: 8-byte INTEGER chunks -> x0/x1 */
	c->nreg = 0;
	for (uint32_t k = 0; 8 * k < sz; k++) {
		c->cls[c->nreg] = MT_I64;
		c->off[c->nreg] = 8 * k;
		c->nreg++;
	}
}

static void
mout_blit(MFnM *fm, MOut *o, MVal *dstp, MVal *srcp, uint32_t size)
{
	MInsM *in = mout_alloc(o);
	in->op = MMOP_BLIT;
	in->src[0] = dstp;
	in->src[1] = srcp;
	in->cst = mconst_int(fm->host, MT_I32, size);
}

/* Next aarch64 argument register of the given class (x0-x7 / v0-v7);
 * NULL when exhausted. */
static MVal *
rarg(MFnM *fm, int *ni, int *ns, bool isf)
{
	const MTargetM *mt = fm->mt;
	int pos = isf ? *ns : *ni;
	if (isf) (*ns)++; else (*ni)++;
	if (pos >= 8)
		return 0;
	int base = isf ? 8 : 0;   /* argreg[0..7]=x0-x7, [8..15]=v0-v7 */
	return reg(fm, mt->argreg[base + pos]);
}

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
int ni = 0, ns = 0;
	int off = 0;    /* AAPCS64: caller writes stack-passed args to
	                * [sp_at_call + 0, +8, ...].  Per AAPCS64 §Frame
	                * Pointer, x29 = sp at function entry = sp_at_call, so
	                * the args land at [x29 + 0].  The save area for fp/lr
	                * is below the args at [x29, -16] (saved x29) and
	                * [x29, -8] (saved x30), not above them. */

	/* aggregate return (sret): x8 holds the hidden buffer; stash it so
	 * selret can reload it after body calls clobber x8 */
	if (fm->retty) {
		A64Class aret;
		a64_classify(&aret, fm->retty);
		if (aret.inmem) {
			MVal *x8 = reg(fm, A64MREG_X8);
			fm->has_sret = true;
			MVal *pad = tmp(fm, MT_PTR, "sret");
			mout(o, MMOP_ALLOCA16, MT_PTR, pad, 0, 0);
			mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(pad, 0, 1, 0), x8);
			fm->sret_pad = pad;
		}
	}

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;

		if (p->td) {
			A64Class c;
			a64_classify(&c, p->td);
			if (c.inmem) {
				/* by-reference aggregate: dst is the pointer received */
				MVal *r = rarg(fm, &ni, &ns, false);
				if (r)
					mout(o, MMOP_MOV, MT_PTR, dst, r, 0);
				continue;
			}
			/* register-passed aggregate: dst points at a fresh pad; the
			 * incoming chunks are stored into [dst] */
			mout(o, MMOP_ALLOCA16, MT_PTR, dst, 0, 0);
			for (int k = 0; k < c.nreg; k++) {
				bool f = (c.cls[k] == MT_F32 || c.cls[k] == MT_F64);
				MVal *r = rarg(fm, &ni, &ns, f);
				if (r)
					mout_addr(o, MMOP_STORE, c.cls[k], 0,
					          maddr(dst, 0, 1, c.off[k]), r);
			}
			continue;
		}

		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, A64MREG_X29), 0, 1, off), 0);
			off += 8;
			continue;
		}
		mout(o, MMOP_MOV, p->dtype, dst, rarg(fm, &ni, &ns, isf), 0);
	}
	/* record the first-unused GPR/FPR counts and the caller-pushed stack
	 * offset for va_start (offset relative to fp) */
	*vafa = (uint32_t)((ni << 8) | (ns << 16) | (off << 24));

	/* varargs: spill x0-x7 into the low 64 bytes of a 192-byte save area
	 * and v0-v7 (d-view) into 16-byte slots, so va_start can set the
	 * __gr_top/__vr_top pointers.  AAPCS64 reg_save_area layout. */
	if (fm->host && fm->host->vararg) {
		MVal *save = tmp(fm, MT_PTR, "va_save");
		mout_cst(o, MMOP_ALLOCA16, MT_PTR, save, 0,
		         imm(fm, MT_I32, 192));
		fm->va_save = save;
		for (int i = 0; i < 8; i++)
			mout_addr(o, MMOP_STORE, MT_I64, 0,
			          maddr(save, 0, 1, i * 8),
			          reg(fm, A64MREG_X0 + i));
		for (int i = 0; i < 8; i++)
			mout_addr(o, MMOP_STORE, MT_F64, 0,
			          maddr(save, 0, 1, 64 + i * 16),
			          reg(fm, A64MREG_V0 + i));
	}
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	int ni = 0, ns = 0;
	int stk = 0;

	/* aggregate return: reserve a pad in OUR frame; call->dst points at
	 * it.  sret (>16B) additionally passes the pad pointer in x8. */
	A64Class aret;
	bool has_aret = call->td != 0;
	if (has_aret)
		a64_classify(&aret, call->td);
	if (has_aret) {
		MVal *pad = tmp(fm, MT_PTR, "abi");
		mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
		         mconst_int(fm->host, MT_I32, aret.size));
		mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
		if (aret.inmem) {
			MVal *x8 = reg(fm, A64MREG_X8);
			mout(o, MMOP_MOV, MT_PTR, x8, call->dst, 0);
		}
	}

	/* count stack-passed scalar overflow args (aggregates are passed by
	 * reference or in regs, never on the stack) */
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->td) {
			A64Class c;
			a64_classify(&c, a->td);
			bool f = (c.nreg && (c.cls[0] == MT_F32 || c.cls[0] == MT_F64));
			int *cur = (c.inmem || !f) ? &ni : &ns;
			if (*cur + (c.inmem ? 1 : c.nreg) > 8)
				stk += 8;   /* by-ref aggregate over 8 GPRs: on stack */
			else
				*cur += c.inmem ? 1 : c.nreg;
			continue;
		}
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8)
			stk += 8;
		else
			{ if (isf) ns++; else ni++; }
	}
	stk = (stk + 15) & ~15;   /* 16-align the overflow area */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, A64MREG_X31),
		         imm(fm, MT_I64, stk));

	ni = 0; ns = 0;
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->td) {
			A64Class c;
			a64_classify(&c, a->td);
			if (c.inmem) {
				/* by-reference: pass the pointer */
				MVal *r = rarg(fm, &ni, &ns, false);
				if (r)
					mout(o, MMOP_MOV, MT_PTR, r, a->src[0], 0);
				continue;
			}
			for (int k = 0; k < c.nreg; k++) {
				bool f = (c.cls[k] == MT_F32 || c.cls[k] == MT_F64);
				MVal *r = rarg(fm, &ni, &ns, f);
				if (r)
					mout_addr(o, MMOP_LOAD, c.cls[k], r,
					          maddr(a->src[0], 0, 1, c.off[k]), 0);
			}
			continue;
		}
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, A64MREG_X31), 0, 1, soff),
			          a->src[0]);
			soff += 8;
			continue;
		}
		MVal *r = rarg(fm, &ni, &ns, isf);
		if (r)
			mout(o, MMOP_MOV, a->dtype, r, a->src[0], 0);
	}

	/* the call itself (dst = result; src[0] = callee) */
	MInsM *mi = mout(o, MMOP_CALL, call->dtype, call->dst, call->src[0], 0);
	mi->td = call->td;

	/* results: land the ≤16B return registers into the pad */
	if (has_aret && !aret.inmem) {
		for (int k = 0; k < aret.nreg; k++) {
			bool f = (aret.cls[k] == MT_F32 || aret.cls[k] == MT_F64);
			MReg rr = f ? (k == 0 ? A64MREG_V0 : A64MREG_V1)
			            : (k == 0 ? A64MREG_X0 : A64MREG_X1);
			mout_addr(o, MMOP_STORE, aret.cls[k], 0,
			          maddr(call->dst, 0, 1, aret.off[k]), reg(fm, rr));
		}
	}
}

/* ---- selret: return lowering ------------------------------------------- */

static void
mabi_selret(MFnM *fm, MOut *o, MInsM *term)
{
	MVal *s0 = term->src[0];
	if (term->td) {
		A64Class c;
		a64_classify(&c, term->td);
		if (!s0) {
			/* dead path: plain return */
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
		if (c.inmem) {
			/* sret: copy the aggregate into the hidden buffer (x8).
			 * x8 may have been clobbered by body calls; reload it from
			 * the slot stashed by selpar. */
			if (fm->sret_pad)
				mout_addr(o, MMOP_LOAD, MT_PTR, reg(fm, A64MREG_X8),
				          maddr(fm->sret_pad, 0, 1, 0), 0);
			mout_blit(fm, o, reg(fm, A64MREG_X8), s0, c.size);
		} else {
			/* ≤16B register return: pack chunks into x0/x1 / v0/v1 */
			for (int k = 0; k < c.nreg; k++) {
				bool f = (c.cls[k] == MT_F32 || c.cls[k] == MT_F64);
				MReg rr = f ? (k == 0 ? A64MREG_V0 : A64MREG_V1)
				            : (k == 0 ? A64MREG_X0 : A64MREG_X1);
				mout_addr(o, MMOP_LOAD, c.cls[k], reg(fm, rr),
				          maddr(s0, 0, 1, c.off[k]), 0);
			}
		}
		term->op = MMOP_RET;
		term->src[0] = 0;
		term->td = 0;
		return;
	}
	if (!s0) {
		term->op = MMOP_RET;
		term->src[0] = 0;
		term->td = 0;
		return;
	}
	bool isf = s0->type == MT_F32 || s0->type == MT_F64;
	mout(o, MMOP_MOV, s0->type, reg(fm, isf ? A64MREG_V0 : A64MREG_X0),
	     s0, 0);
	/* the RET terminator now returns the value already in the register */
	term->op = MMOP_RET;
	term->src[0] = 0;
	term->td = 0;
}

/* ---- va_start / va_arg --------------------------------------------------
 * AAPCS64 va_list is a 32-byte structure:
 *     0  : void *__stack   next stack argument
 *     8  : void *__gr_top  end of the GP save area
 *    16  : void *__vr_top  end of the FP save area
 *    24  : int   __gr_offs GP offset (negative from gr_top)
 *    28  : int   __vr_offs FP offset (negative from vr_top)
 * va_arg reads __gr_offs/__vr_offs: while negative the value comes from
 * the save area (gr_top + offs), once >= 0 it comes from __stack.  The
 * branchless select (mask/xor/and) mirrors the x86_64 mabi. */

static void
mabi_vastart(MFnM *fm, MOut *o, MVal *ap, uint32_t vafa)
{
	int ngp = (vafa >> 8) & 15;    /* consumed GP registers */
	int soff = vafa >> 24;         /* first stack arg offset from fp */
	MVal *fp = reg(fm, A64MREG_X29);

	/* __stack = fp + soff */
	MVal *stk = tmp(fm, MT_PTR, "abi");
	mout_addr(o, MMOP_LEA, MT_PTR, stk, maddr(fp, 0, 1, soff), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), stk);

	/* __gr_top = va_save + 64, __vr_top = va_save + 192 */
	if (fm->va_save) {
		MVal *gt = tmp(fm, MT_PTR, "abi");
		mout_addr(o, MMOP_LEA, MT_PTR, gt, maddr(fm->va_save, 0, 1, 64), 0);
		mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 8), gt);
		MVal *vt = tmp(fm, MT_PTR, "abi");
		mout_addr(o, MMOP_LEA, MT_PTR, vt, maddr(fm->va_save, 0, 1, 192), 0);
		mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 16), vt);
	}

	/* __gr_offs = (ngp - 8) * 8, __vr_offs = (nfp - 8) * 16 */
	int nfp = (vafa >> 16) & 15;
	mout_cst(o, MMOP_STORE, MT_I32, 0, 0,
	         imm(fm, MT_I32, (ngp - 8) * 8));
	o->ins[o->nins - 1].addr = maddr(ap, 0, 1, 24);
	mout_cst(o, MMOP_STORE, MT_I32, 0, 0,
	         imm(fm, MT_I32, (nfp - 8) * 16));
	o->ins[o->nins - 1].addr = maddr(ap, 0, 1, 28);
}

static void
mabi_vaarg(MFnM *fm, MOut *o, MInsM *in)
{
	MVal *ap = in->src[0];
	MVal *dst = in->dst;
	bool isf = in->dtype == MT_F32 || in->dtype == MT_F64;
	int ooff = isf ? 28 : 24;      /* __vr_offs / __gr_offs field */
	int top = isf ? 16 : 8;        /* __vr_top / __gr_top field */
	int step = isf ? 16 : 8;       /* per-argument advance */

	/* off = current __gr_offs / __vr_offs (signed, negative while the
	 * save area still has unconsumed registers) */
	MVal *off = tmp(fm, MT_I32, "va");
	mout_addr(o, MMOP_LOAD, MT_I32, off, maddr(ap, 0, 1, ooff), 0);
	/* in_reg = off < 0 (signed) */
	MVal *zero = mval_const(fm->host, MT_I64, imm(fm, MT_I64, 0));
	MVal *inr = tmp(fm, MT_I32, "va");
	MInsM *sc = mout(o, MMOP_SETCCR, MT_I32, inr, off, zero);
	sc->cc = MCC_LT;
	/* mask = in_reg ? -1 : 0  (64-bit) */
	MVal *inr64 = tmp(fm, MT_I64, "va");
	mout(o, MMOP_MOVSX, MT_I64, inr64, inr, 0);
	MVal *mask = tmp(fm, MT_I64, "va");
	mout(o, MMOP_NEG, MT_I64, mask, inr64, 0);

	/* reg path address = top + off (off is negative: sign-extend) */
	MVal *topv = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, topv, maddr(ap, 0, 1, top), 0);
	MVal *off64 = tmp(fm, MT_I64, "va");
	mout(o, MMOP_MOVSX, MT_I64, off64, off, 0);
	MVal *rega = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, rega, topv, off64);
	/* stack path address = __stack */
	MVal *stkp = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, stkp, maddr(ap, 0, 1, 0), 0);

	/* addr = in_reg ? rega : stkp (branchless select) */
	MVal *diff = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, diff, rega, stkp);
	MVal *sel = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_AND, MT_PTR, sel, diff, mask);
	MVal *addr = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, addr, stkp, sel);

	/* dst = *addr */
	mout_addr(o, MMOP_LOAD, in->dtype, dst, maddr(addr, 0, 1, 0), 0);

	/* advance: reg path bumps the offset field by step (save-area slot
	 * width: 8 GP / 16 FP); stack path bumps __stack by 8 (every AAPCS64
	 * stack argument occupies an 8-byte slot).  in_reg -> mask is -1. */
	MVal *incr = tmp(fm, MT_I64, "va");
	mout_cst(o, MMOP_AND, MT_I64, incr, mask, imm(fm, MT_I64, step));
	MVal *nof = tmp(fm, MT_I64, "va");
	mout(o, MMOP_ADD, MT_I64, nof, off64, incr);
	mout_addr(o, MMOP_STORE, MT_I32, 0, maddr(ap, 0, 1, ooff), nof);
	MVal *nstk = tmp(fm, MT_I64, "va");
	mout(o, MMOP_NOT, MT_I64, nstk, mask, 0);
	mout_cst(o, MMOP_AND, MT_I64, nstk, nstk, imm(fm, MT_I64, 8));
	MVal *nsp = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, nsp, stkp, nstk);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), nsp);
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
		if (in->op == MMOP_VASTART) {
			mabi_vastart(fm, o, in->src[0], fm->vafa);
			continue;
		}
		if (in->op == MMOP_VAARG) {
			mabi_vaarg(fm, o, in);
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
mfnm_abi_aarch64(MFnM *fm)
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
