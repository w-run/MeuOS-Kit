/* riscv64_mabi.c — riscv64 ABI lowering (MIR-native, LP64D scalar core).
 *
 * Lowers the pre-ABI machine IR for scalar integer/float functions:
 *   - selpar: move entry a0-a7/fa0-fa7 arguments into slots (overflow
 *     arguments are loaded from the caller-pushed stack area at fp+off);
 *   - selcall: move call arguments into a0-a7/fa0-fa7 (overflow onto the
 *     stack below sp);
 *   - selret: move the return value into a0 / fa0.
 *
 * Aggregates, varargs, floats-ops, TLS and VLA functions are rejected by
 * the riscv64 isel (mbe_supported), so this scalar core never sees them.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "riscv64_m.h"

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

/* ---- aggregate classification (LP64D) -----------------------------------
 * An aggregate is passed/returned in up to two 8-byte chunks:
 *   - all-FP structs/arrays (every scalar leaf is float/double, not a
 *     union) use fa0/fa1 — matches the legacy fpstruct() model;
 *   - anything else ≤16B is split into 8-byte INTEGER chunks (a0/a1);
 *   - size 0 / incomplete / >16B is passed/returned by reference (sret). */
typedef struct RvClass {
	MTypeDesc *td;
	uint32_t size;
	int nreg;          /* 0..2 */
	MType cls[2];      /* per-chunk scalar type (MT_I64 or MT_F64) */
	uint32_t off[2];   /* byte offset of each chunk within the aggregate */
	bool inmem;
} RvClass;

/* Recursive all-FP check: fills cls/off for FP chunks, returns the FP
 * chunk count, or -1 when the aggregate has a non-FP leaf / is a union /
 * would need a third chunk. */
static int
rv_fpstruct(MTypeDesc *td, int off, RvClass *c)
{
	if (td->is_union)
		return -1;
	if (td->is_array) {
		if (td->elem_type == MT_AGG)
			return rv_fpstruct(td->elem_desc, off, c);
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
			if (rv_fpstruct(f->sub, off + (int)f->offset, c) == -1)
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
rv_classify(RvClass *c, MTypeDesc *td)
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
	if (rv_fpstruct(td, 0, c) >= 0)
		return;
	/* non-FP aggregate: 8-byte INTEGER chunks -> a0/a1 */
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

/* Next riscv64 argument register of the given class (a0-a7 / fa0-fa7);
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

	/* aggregate return (sret): a0 holds the hidden buffer; stash it so
	 * selret can reload it after body calls clobber a0 */
	if (fm->retty) {
		RvClass aret;
		rv_classify(&aret, fm->retty);
		if (aret.inmem) {
			MVal *a0 = rarg(fm, &ni, &ns, false);
			fm->has_sret = true;
			MVal *pad = tmp(fm, MT_PTR, "sret");
			mout(o, MMOP_ALLOCA16, MT_PTR, pad, 0, 0);
			mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(pad, 0, 1, 0), a0);
			fm->sret_pad = pad;
		}
	}

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;

		if (p->td) {
			RvClass c;
			rv_classify(&c, p->td);
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
				MVal *r = rarg(fm, &ni, &ns, c.cls[k] == MT_F64);
				if (r)
					mout_addr(o, MMOP_STORE, c.cls[k], 0,
					          maddr(dst, 0, 1, c.off[k]), r);
			}
			continue;
		}

		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, RV64MREG_FP), 0, 1, off), 0);
			off += 8;
			continue;
		}
		mout(o, MMOP_MOV, p->dtype, dst, rarg(fm, &ni, &ns, isf), 0);
	}
	/* record the first-unused GPR/FPR counts and the caller-pushed stack
	 * offset for va_start (offset relative to fp) */
	*vafa = (uint32_t)((ni << 8) | (ns << 16) | (off << 24));

	/* varargs: spill every possible GP argument register into a 64-byte
	 * save area so va_start can point the va_list (a single pointer on
	 * riscv64) at the first unconsumed register argument.  Matches the
	 * legacy pointer-based va_list: va_arg reads *ap and advances by 8. */
	if (fm->host && fm->host->vararg) {
		MVal *save = tmp(fm, MT_PTR, "va_save");
		mout_cst(o, MMOP_ALLOCA16, MT_PTR, save, 0,
		         imm(fm, MT_I32, 64));
		fm->va_save = save;
		for (int i = 0; i < 8; i++)
			mout_addr(o, MMOP_STORE, MT_I64, 0,
			          maddr(save, 0, 1, i * 8),
			          reg(fm, RV64MREG_A0 + i));
	}
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	int ni = 0, ns = 0;
	int stk = 0;

	/* aggregate return: reserve a pad in OUR frame; call->dst points at
	 * it.  sret (>16B) additionally passes the pad pointer in a0. */
	RvClass aret;
	bool has_aret = call->td != 0;
	if (has_aret)
		rv_classify(&aret, call->td);
	if (has_aret) {
		MVal *pad = tmp(fm, MT_PTR, "abi");
		mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
		         mconst_int(fm->host, MT_I32, aret.size));
		mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
		if (aret.inmem) {
			MVal *a0 = rarg(fm, &ni, &ns, false);
			mout(o, MMOP_MOV, MT_PTR, a0, call->dst, 0);
		}
	}

	/* count stack-passed scalar overflow args (aggregates are passed by
	 * reference or in regs, never on the stack) */
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->td) {
			RvClass c;
			rv_classify(&c, a->td);
			int *cur = (c.inmem || (c.nreg && c.cls[0] != MT_F64))
			           ? &ni : &ns;
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
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, RV64MREG_SP),
		         imm(fm, MT_I64, stk));

	ni = 0; ns = 0;
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->td) {
			RvClass c;
			rv_classify(&c, a->td);
			if (c.inmem) {
				/* by-reference: pass the pointer */
				MVal *r = rarg(fm, &ni, &ns, false);
				if (r)
					mout(o, MMOP_MOV, MT_PTR, r, a->src[0], 0);
				continue;
			}
			for (int k = 0; k < c.nreg; k++) {
				MVal *r = rarg(fm, &ni, &ns, c.cls[k] == MT_F64);
				if (r)
					mout_addr(o, MMOP_LOAD, c.cls[k], r,
					          maddr(a->src[0], 0, 1, c.off[k]), 0);
			}
			continue;
		}
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 8) {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, RV64MREG_SP), 0, 1, soff),
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
			MReg rr = aret.cls[k] == MT_F64
			          ? (k == 0 ? RV64MREG_FA0 : RV64MREG_FA1)
			          : (k == 0 ? RV64MREG_A0 : RV64MREG_A1);
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
		RvClass c;
		rv_classify(&c, term->td);
		if (!s0) {
			/* dead path: plain return */
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
		if (c.inmem) {
			/* sret: copy the aggregate into the hidden buffer (a0).
			 * a0 may have been clobbered by body calls; reload it from
			 * the slot stashed by selpar. */
			if (fm->sret_pad)
				mout_addr(o, MMOP_LOAD, MT_PTR, reg(fm, RV64MREG_A0),
				          maddr(fm->sret_pad, 0, 1, 0), 0);
			mout_blit(fm, o, reg(fm, RV64MREG_A0), s0, c.size);
		} else {
			/* ≤16B register return: pack chunks into a0/a1 / fa0/fa1 */
			for (int k = 0; k < c.nreg; k++) {
				MReg rr = c.cls[k] == MT_F64
				          ? (k == 0 ? RV64MREG_FA0 : RV64MREG_FA1)
				          : (k == 0 ? RV64MREG_A0 : RV64MREG_A1);
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
	mout(o, MMOP_MOV, s0->type, reg(fm, isf ? RV64MREG_FA0 : RV64MREG_A0),
	     s0, 0);
	/* the RET terminator now returns the value already in the register */
	term->op = MMOP_RET;
	term->src[0] = 0;
	term->td = 0;
}

/* ---- va_start / va_arg --------------------------------------------------
 * riscv64 va_list is a single pointer (targ.c typevalist): *ap points at
 * the next vararg.  va_start points it at the first unconsumed GP
 * register (saved in the 64-byte va_save area) or, when all GP registers
 * were consumed by named parameters, at the caller-pushed stack args.
 * va_arg reads *(type*)*ap and advances *ap by 8 — mirrors the legacy
 * rv64_abi.c selvastart/selvaarg. */

static void
mabi_vastart(MFnM *fm, MOut *o, MVal *ap, uint32_t vafa)
{
	int gp = (vafa >> 8) & 15;    /* consumed GP registers */
	int soff = vafa >> 24;        /* first stack arg offset from fp */
	MVal *first;

	if (gp < 8 && fm->va_save) {
		/* first unconsumed GP arg lives at va_save + gp*8 */
		first = tmp(fm, MT_PTR, "abi");
		mout_addr(o, MMOP_LEA, MT_PTR, first,
		          maddr(fm->va_save, 0, 1, gp * 8), 0);
	} else {
		/* all GP regs consumed: the varargs continue on the stack */
		first = tmp(fm, MT_PTR, "abi");
		mout_addr(o, MMOP_LEA, MT_PTR, first,
		          maddr(reg(fm, RV64MREG_FP), 0, 1, soff), 0);
	}
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), first);
}

static void
mabi_vaarg(MFnM *fm, MOut *o, MInsM *in)
{
	MVal *ap = in->src[0];
	MVal *dst = in->dst;

	/* loc = *ap; *ap = loc + 8; dst = *(type*)loc */
	MVal *loc = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, loc, maddr(ap, 0, 1, 0), 0);
	MVal *nl = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, nl, loc,
	     mval_const(fm->host, MT_I64, imm(fm, MT_I64, 8)));
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 0), nl);
	mout_addr(o, MMOP_LOAD, in->dtype, dst, maddr(loc, 0, 1, 0), 0);
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
mfnm_abi_riscv64(MFnM *fm)
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
