/* x86_64_mabi.c — System V ABI lowering for the MIR machine layer (P2).
 *
 * Ports the proven classification / lowering rules of x86_64_sysv.c
 * (typclass / argsclass / retr / selpar / selcall / selret / selvaarg /
 * selvastart) onto MIR-native data: MFnM + MTypeDesc/MField instead of
 * LIR Fn + typ[]/Field.  Purity rule: no QBE structures; only the
 * "correctness rules" are carried over, re-expressed in MMOP/mfnm terms.
 *
 * Input convention (produced by func_to_mir / P3 isel, or by hand):
 *   - entry block:  MMOP_PARM dst=value  (td set for aggregate params;
 *                   dst is then a pointer to the callee-side storage pad)
 *   - block body:   MMOP_ARG src[0]=value (td set for aggregate args;
 *                   src[0] is then a pointer to the aggregate data)
 *                   MMOP_CALL src[0]=callee, dst=result (td set for
 *                   aggregate return; dst is then the sret pad pointer)
 *   - terminator:   MMOP_RET src[0]=return value (td set for aggregate
 *                   return: src[0] is a pointer to the aggregate)
 *                   MMOP_VASTART src[0]=ap / MMOP_VAARG dst, src[0]=ap
 *
 * The lowering rewrites each block, replacing the markers with concrete
 * register/stack moves required by the ABI.
 *
 * Varargs note: the SysV va_list structure and the reg/overflow paths are
 * lowered here; the P2 version emits a conservative single-path expansion
 * (register save area only).  The full split with phi-merge lands with the
 * regalloc work in P4/P5.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"

/* ---- classification ----------------------------------------------------- */

/* classify one scalar at byte offset `off` into the eightbyte slots */
static void
mabi_classify_scalar(MAClass *a, MType t, uint32_t off)
{
	if (t == MT_NONE || t == MT_VOID)
		return;
	MType *cls = &a->cls[off / 8];
	if (t == MT_F32 || t == MT_F64) {
		if (*cls == MT_NONE)
			*cls = MT_F64;
	} else {
		*cls = MT_I64;
	}
}

static void
mabi_classify(MAClass *a, MTypeDesc *td, uint32_t s)
{
	if (td->is_array) {
		uint64_t esz = td->elem_desc ? td->elem_desc->size
		                             : mtypesize(td->elem_type);
		if (td->elem_type == MT_NONE && !td->elem_desc)
			return;
		for (uint64_t i = 0; i < td->nelem; i++) {
			uint32_t off = s + i * esz;
			if (td->elem_desc &&
			    (td->elem_desc->nfield || td->elem_desc->is_array)) {
				/* aggregate element: recurse */
				mabi_classify(a, td->elem_desc, off);
			} else if (td->elem_desc) {
				/* scalar carrier: its elem_type is the scalar type */
				mabi_classify_scalar(a, td->elem_desc->elem_type, off);
			} else {
				mabi_classify_scalar(a, td->elem_type, off);
			}
		}
		return;
	}
	for (uint32_t i = 0; i < td->nfield; i++) {
		MField *f = &td->field[i];
		if (f->type == MT_AGG) {
			mabi_classify(a, f->sub, s + f->offset);
		} else {
			mabi_classify_scalar(a, f->type, s + f->offset);
		}
	}
}

void
mabi_typclass(MAClass *a, MTypeDesc *td)
{
	uint32_t sz = td->size;
	uint32_t al = td->align < 8 ? 8 : td->align;
	sz = (sz + al - 1) & ~(al - 1);

	a->td = td;
	a->size = sz;
	a->align = td->align >= 16 ? 4 : 3;

	if (td->is_incomplete || sz > 16 || sz == 0) {
		a->inmem = 1;
		return;
	}
	a->cls[0] = a->cls[1] = MT_NONE;
	a->inmem = 0;
	mabi_classify(a, td, 0);
	/* An eightbyte that classify() left untouched is INTEGER per the SysV
	 * psABI — e.g. a C++ `class Empty {}` (size 1, no data members) has
	 * no field to land in the slot.  Leaving it MT_NONE made the caller
	 * and callee disagree on the argument register: the caller put the
	 * value in RDI while the callee read RSI for `&e` (verified under
	 * MCC_MIR_BACKEND=1, 2026-08-03).  Mirrors the LIR fix in 2be27a7. */
	for (uint32_t k = 0; k * 8 < sz; k++)
		if (a->cls[k] == MT_NONE)
			a->cls[k] = MT_I64;
}

int
mabi_argsclass(MFnM *fm, MInsM *m, int n, MAClass *ac, MAClass *aret)
{
	(void)fm;
	int nint = aret && aret->inmem ? 5 : 6;
	int nsse = 8;

	for (int i = 0; i < n; i++, m++) {
		if (m->td) {
			mabi_typclass(&ac[i], m->td);
			if (ac[i].inmem)
				continue;
			int ni = 0, ns = 0;
			for (uint32_t k = 0; k * 8 < ac[i].size; k++)
				if (ac[i].cls[k] == MT_I64)
					ni++;
				else if (ac[i].cls[k] == MT_F64)
					ns++;
			if (nint >= ni && nsse >= ns) {
				nint -= ni;
				nsse -= ns;
			} else {
				ac[i].inmem = 1;
			}
		} else {
			bool isf = m->dtype == MT_F32 || m->dtype == MT_F64;
			ac[i].size = 8;
			ac[i].align = 3;
			ac[i].cls[0] = isf ? MT_F64 : MT_I64;
			if (isf) {
				if (nsse > 0) { nsse--; ac[i].inmem = 0; }
				else          ac[i].inmem = 2;
			} else {
				if (nint > 0) { nint--; ac[i].inmem = 0; }
				else          ac[i].inmem = 2;
			}
		}
	}
	return ((6 - nint) << 4) | ((8 - nsse) << 8);
}

static int
mabi_retn(MAClass *a)
{
	int n = 0;
	for (uint32_t k = 0; k * 8 < a->size; k++)
		if (a->cls[k] != MT_NONE)
			n++;
	return n;
}

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

static MInsM *
mout_blit(MFnM *fm, MOut *o, MVal *dstp, MVal *srcp, uint32_t size)
{
	MInsM *in = mout_alloc(o);
	in->op = MMOP_BLIT;
	in->src[0] = dstp;
	in->src[1] = srcp;
	in->cst = mconst_int(fm->host, MT_I32, size);
	return in;
}

/* ---- helpers ------------------------------------------------------------ */

static MVal *
reg(MFnM *fm, int x64reg)
{
	return mfn_reg(fm->host, fm->mt, x64reg);
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

/* Next SysV argument register of the given class; NULL when exhausted. */
static MVal *
rarg(MFnM *fm, int *ni, int *ns, bool isf)
{
	const MTargetM *mt = fm->mt;
	int base = isf ? 6 : 0;
	int pos = isf ? *ns : *ni;
	int max = isf ? 8 : 6;
	if (pos >= max || mt->argreg[base + pos] < 0)
		return 0;
	if (isf) (*ns)++; else (*ni)++;
	return reg(fm, mt->argreg[base + pos]);
}

/* ---- selpar: entry parameter reception --------------------------------- */

static void
mabi_selpar(MFnM *fm, MOut *o, MInsM *parms, int n, uint32_t *vafa)
{
	MAClass *ac = calloc(n, sizeof *ac);
	MAClass aret;
	MAClass *pa = 0;
	int ni = 0, ns = 0;
	/* caller-pushed stack args start at rbp+16 (after push rbp + the
	 * return address); the frame pointer is stable once the prologue runs */
	int off = 16;

	if (fm->retty) {
		mabi_typclass(&aret, fm->retty);
		if (aret.inmem) {
			/* RDI = hidden sret pad.  RDI is caller-saved: any call in the
			 * body clobbers it, so stash it in an alloca slot (regalloc
			 * manages the slot across calls) and reload it in selret. */
			MVal *rdi = rarg(fm, &ni, &ns, false);
			fm->has_sret = true;   /* sret buffer in mt->sret_reg (RDI); pin it for regalloc */
			MVal *pad = tmp(fm, MT_PTR, "sret");
			mout(o, MMOP_ALLOCA16, MT_PTR, pad, 0, 0);
			mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(pad, 0, 1, 0), rdi);
			fm->sret_pad = pad;
		}
		pa = &aret;
	}

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;

		if (p->td) {
			mabi_typclass(&ac[i], p->td);
			if (ac[i].inmem) {
				/* caller pushed the aggregate; dst addresses it */
				mout_addr(o, MMOP_LEA, MT_PTR, dst,
				          maddr(reg(fm, X64MREG_RBP), 0, 1, off), 0);
				off += ac[i].size;
				continue;
			}
			if (ac[i].cls[0] != MT_NONE) {
				/* register-passed aggregate: dst must point at a pad; a
				 * static alloca reserves it in the caller's frame */
				mout(o, MMOP_ALLOCA16, MT_PTR, dst, 0, 0);
				MVal *r0 = rarg(fm, &ni, &ns, ac[i].cls[0] == MT_F64);
				if (r0)
					mout_addr(o, MMOP_STORE,
					          ac[i].cls[0] == MT_F64 ? MT_F64 : MT_I64,
					          0, maddr(dst, 0, 1, 0), r0);
			}
			if (ac[i].size > 8 && ac[i].cls[1] != MT_NONE) {
				MVal *r1 = rarg(fm, &ni, &ns, ac[i].cls[1] == MT_F64);
				if (r1)
					mout_addr(o, MMOP_STORE,
					          ac[i].cls[1] == MT_F64 ? MT_F64 : MT_I64,
					          0, maddr(dst, 0, 1, 8), r1);
			}
			continue;
		}

		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;
		if (isf ? ns >= 8 : ni >= 6) {
			mout_addr(o, MMOP_LOAD, p->dtype, dst,
			          maddr(reg(fm, X64MREG_RBP), 0, 1, off), 0);
			off += 8;
			continue;
		}
		mout(o, MMOP_MOV, p->dtype, dst, rarg(fm, &ni, &ns, isf), 0);
	}

	/* varargs: spill every possible argument register into the
	 * reg_save_area (rbp-176) so va_arg can read them later */
	if (fm->host && fm->host->vararg) {
		MVal *rbp = reg(fm, X64MREG_RBP);
		static const int gprs[6] = {
			X64MREG_RDI, X64MREG_RSI, X64MREG_RDX, X64MREG_RCX,
			X64MREG_R8, X64MREG_R9,
		};
		for (int i = 0; i < 6; i++)
			mout_addr(o, MMOP_STORE, MT_I64, 0,
			          maddr(rbp, 0, 1, -176 + i * 8),
			          reg(fm, gprs[i]));
		for (int i = 0; i < 8; i++)
			mout_addr(o, MMOP_STORE, MT_F64, 0,
			          maddr(rbp, 0, 1, -176 + 48 + i * 16),
			          reg(fm, X64MREG_XMM0 + i));
	}

	/* record varargs register usage for selvastart: packed offsets of
	 * the first unused GPR/XMM (counts of registers already consumed)
	 * plus the byte offset (from rbp) of the first caller-pushed stack
	 * argument (off starts at 16 = rbp+16, below the saved rbp and the
	 * return address).  va_start stores this as overflow_arg_area. */
	(void)pa;
	*vafa = (ni << 4) | (ns << 8) | (off << 12);
	free(ac);
}

/* ---- selcall: call lowering --------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	MAClass *ac = calloc(n, sizeof *ac);
	MAClass aret, *pa = 0;
	int ni = 0, ns = 0;

	if (call->td) {
		mabi_typclass(&aret, call->td);
		pa = &aret;
	}
	mabi_argsclass(fm, args, n, ac, pa);

	/* stack argument space (16-aligned) */
	uint32_t stk = 0;
	for (int i = 0; i < n; i++)
		if (ac[i].inmem) {
			if (ac[i].align == 4)
				stk += stk & 15;
			stk += ac[i].size;
		}
	stk += stk & 15;
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, X64MREG_RSP),
		         imm(fm, MT_I64, stk));

	/* aggregate return */
	if (pa) {
		if (aret.inmem) {
			/* hidden sret pointer in RDI: the call's dst is a fresh
			 * value (SSA-defined by the call itself), so reserve a
			 * pad in OUR frame, store its address into the call dst
			 * (the post-call load reads the pad) and pass it in RDI */
			MVal *pad = tmp(fm, MT_PTR, "abi");
			mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
			         mconst_int(fm->host, MT_I32, aret.size));
			mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
			MVal *rdi = rarg(fm, &ni, &ns, false);
			mout(o, MMOP_MOV, MT_PTR, rdi, call->dst, 0);
		} else {
			/* ≤16B aggregate return: the SysV ABI returns the struct in
			 * RAX/RDX (INTEGER/SSE eightbytes) or XMM0/XMM1 — the callee
			 * side (selret) packs them there.  The post-call sequence
			 * still treats call->dst as a pad pointer (store/load
			 * pattern), so reserve a pad in OUR frame and point
			 * call->dst at it; the "results" section below lands the
			 * return registers into that pad.  No hidden RDI argument
			 * for register returns (unlike the >16B sret case). */
			MVal *pad = tmp(fm, MT_PTR, "abi");
			mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
			         mconst_int(fm->host, MT_I32, aret.size));
			mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
		}
	}

	/* argument moves: store stack-passed args FIRST, then load register
	 * args.  memit routes floating-point stores through %xmm0, so if a
	 * stack FP arg were stored after the register args were set up it
	 * would clobber the FP arg register it is about to fill (SysV passes
	 * FP args in xmm0-7, and >8 FP args spill the rest onto the stack). */
	uint32_t soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (!ac[i].inmem)
			continue;
		if (a->td) {
			MVal *dstp = tmp(fm, MT_PTR, "abi");
			mout_addr(o, MMOP_LEA, MT_PTR, dstp,
			          maddr(reg(fm, X64MREG_RSP), 0, 1, soff), 0);
			mout_blit(fm, o, dstp, a->src[0], ac[i].size);
		} else {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, X64MREG_RSP), 0, 1, soff), a->src[0]);
		}
		soff += ac[i].size;
	}
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (ac[i].inmem)
			continue;
		if (a->td) {
			if (ac[i].cls[0] != MT_NONE) {
				MVal *r0 = rarg(fm, &ni, &ns, ac[i].cls[0] == MT_F64);
				if (r0)
					mout_addr(o, MMOP_LOAD,
					          ac[i].cls[0] == MT_F64 ? MT_F64 : MT_I64,
					          r0, maddr(a->src[0], 0, 1, 0), 0);
			}
			if (ac[i].size > 8 && ac[i].cls[1] != MT_NONE) {
				MVal *r1 = rarg(fm, &ni, &ns, ac[i].cls[1] == MT_F64);
				if (r1)
					mout_addr(o, MMOP_LOAD,
					          ac[i].cls[1] == MT_F64 ? MT_F64 : MT_I64,
					          r1, maddr(a->src[0], 0, 1, 8), 0);
			}
		} else {
			bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
			MVal *r = rarg(fm, &ni, &ns, isf);
			if (r)
				mout(o, MMOP_MOV, a->dtype, r, a->src[0], 0);
		}
	}

	/* Set %al to the number of XMM registers used (x86_64 SysV ABI
	 * for variadic calls: the callee's va_start needs this to know
	 * how many vector registers to save to the register save area). */
	if (ns > 0)
		mout_cst(o, MMOP_MOV, MT_I32, reg(fm, X64MREG_RAX), 0,
		         imm(fm, MT_I32, ns));

	/* the call itself */
	mout(o, MMOP_CALL, call->dtype, call->dst, call->src[0], 0);
	o->ins[o->nins - 1].td = call->td;
	/* caller cleanup: restore the stack-argument space reserved above */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, X64MREG_RSP),
		         imm(fm, MT_I64, -(int64_t)stk));

	/* results */
	if (pa) {
		if (aret.inmem) {
			/* RAX returns the pad pointer */
			mout(o, MMOP_MOV, MT_PTR, call->dst, reg(fm, X64MREG_RAX), 0);
	} else {
		/* ≤16B register return: land the per-class SysV register
		 * sequence (INTEGER: rax, rdx; SSE: xmm0, xmm1) into the pad —
		 * mirrors LIR retr().  A positional slot1→XMM1/RDX map breaks
		 * mixed {INTEGER,SSE} returns (the SSE eightbyte must use xmm0). */
		static const int retreg[2][2] = {
			{ X64MREG_RAX, X64MREG_RDX },
			{ X64MREG_XMM0, X64MREG_XMM1 },
		};
		int nr[2] = { 0, 0 };
		for (uint32_t n = 0; n * 8 < aret.size; n++) {
			if (aret.cls[n] == MT_NONE)
				continue;
			int k = aret.cls[n] == MT_F64 ? 1 : 0;
			MReg r = retreg[k][nr[k]++];
			mout_addr(o, MMOP_STORE,
			          aret.cls[n] == MT_F64 ? MT_F64 : MT_I64, 0,
			          maddr(call->dst, 0, 1, n * 8),
			          reg(fm, r));
		}
	}
	} else if (call->dst) {
		bool isf = call->dtype == MT_F32 || call->dtype == MT_F64;
		mout(o, MMOP_MOV, call->dtype, call->dst,
		     reg(fm, isf ? X64MREG_XMM0 : X64MREG_RAX), 0);
	}
	free(ac);
}

/* ---- selret: return lowering --------------------------------------------- */

static void
mabi_selret(MFnM *fm, MOut *o, MInsM *term)
{
	if (term->td) {
		MAClass aret;
		mabi_typclass(&aret, term->td);
		if (!term->src[0]) {
			/* dead path (e.g. a switch join after per-case rets):
			 * nothing to pack, just emit the plain return */
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
		if (aret.inmem) {
			/* sret: copy the aggregate into the hidden return buffer.
			 * RDI may have been clobbered by body calls, so reload the
			 * pad address from the alloca slot stashed by selpar. */
			if (fm->sret_pad)
				mout_addr(o, MMOP_LOAD, MT_PTR, reg(fm, X64MREG_RDI),
				          maddr(fm->sret_pad, 0, 1, 0), 0);
			mout_blit(fm, o, reg(fm, X64MREG_RDI), term->src[0], aret.size);
			mout(o, MMOP_MOV, MT_PTR, reg(fm, X64MREG_RAX),
			     reg(fm, X64MREG_RDI), 0);
		} else {
			/* ≤16B register return: pack each eightbyte into its
			 * per-class SysV register (INTEGER: rax, rdx; SSE: xmm0,
			 * xmm1) — mirrors LIR retr().  A positional slot1→XMM1/RDX
			 * map breaks mixed {INTEGER,SSE} returns. */
			static const int retreg[2][2] = {
				{ X64MREG_RAX, X64MREG_RDX },
				{ X64MREG_XMM0, X64MREG_XMM1 },
			};
			int nr[2] = { 0, 0 };
			for (uint32_t n = 0; n * 8 < aret.size; n++) {
				if (aret.cls[n] == MT_NONE)
					continue;
				int k = aret.cls[n] == MT_F64 ? 1 : 0;
				MReg r = retreg[k][nr[k]++];
				mout_addr(o, MMOP_LOAD,
				          aret.cls[n] == MT_F64 ? MT_F64 : MT_I64,
				          reg(fm, r), maddr(term->src[0], 0, 1, n * 8),
				          0);
			}
		}
	} else if (term->src[0]) {
		bool isf = term->dtype == MT_F32 || term->dtype == MT_F64;
		mout(o, MMOP_MOV, term->dtype,
		     reg(fm, isf ? X64MREG_XMM0 : X64MREG_RAX), term->src[0], 0);
	}
	term->op = MMOP_RET;
	term->src[0] = 0;
	term->td = 0;
}

/* ---- varargs ------------------------------------------------------------- */

static void
mabi_vastart(MFnM *fm, MOut *o, MVal *ap, uint32_t vafa)
{
	MVal *rbp = reg(fm, X64MREG_RBP);
	int gp = ((vafa >> 4) & 15) * 8;
	int fp = 48 + ((vafa >> 8) & 15) * 16;
	int sp = vafa >> 12;

	mout_cst(o, MMOP_STORE, MT_I32, 0, 0, imm(fm, MT_I32, gp));
	o->ins[o->nins - 1].addr = maddr(ap, 0, 1, 0);
	mout_cst(o, MMOP_STORE, MT_I32, 0, 0, imm(fm, MT_I32, fp));
	o->ins[o->nins - 1].addr = maddr(ap, 0, 1, 4);
	MVal *oa = tmp(fm, MT_PTR, "abi");
	mout_addr(o, MMOP_LEA, MT_PTR, oa, maddr(rbp, 0, 1, sp), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 8), oa);
	MVal *rs = tmp(fm, MT_PTR, "abi");
	mout_addr(o, MMOP_LEA, MT_PTR, rs, maddr(rbp, 0, 1, -176), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 16), rs);
}

static void
mabi_vaarg(MFnM *fm, MOut *o, MInsM *in)
{
	MVal *ap = in->src[0];
	MVal *dst = in->dst;
	bool isf = in->dtype == MT_F32 || in->dtype == MT_F64;
	int ooff = isf ? 4 : 0;      /* gp_offset / fp_offset field */
	int oinc = isf ? 16 : 8;     /* per-argument advance */
	int limit = isf ? 176 : 48;  /* reg_save_area bound */

	/* offset = current gp_offset / fp_offset */
	MVal *off = tmp(fm, MT_I32, "va");
	mout_addr(o, MMOP_LOAD, MT_I32, off, maddr(ap, 0, 1, ooff), 0);
	/* in_reg = offset < limit (unsigned) */
	mout_cst(o, MMOP_CMP, MT_I32, 0, off, imm(fm, MT_I32, limit));
	MVal *inr = tmp(fm, MT_I32, "va");
	MInsM *sc = mout(o, MMOP_SETCC, MT_I32, inr, 0, 0);
	sc->cc = MCC_CC;
	/* mask = in_reg ? -1 : 0  (64-bit) */
	MVal *inr64 = tmp(fm, MT_I64, "va");
	mout(o, MMOP_MOVZX, MT_I64, inr64, inr, 0);
	MVal *mask = tmp(fm, MT_I64, "va");
	mout(o, MMOP_NEG, MT_I64, mask, inr64, 0);

	/* reg path address = reg_save_area + offset */
	MVal *regp = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, regp, maddr(ap, 0, 1, 16), 0);
	MVal *off64 = tmp(fm, MT_I64, "va");
	mout(o, MMOP_MOVZX, MT_I64, off64, off, 0);
	MVal *rega = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, rega, regp, off64);
	/* overflow path address = overflow_arg_area */
	MVal *stkp = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, stkp, maddr(ap, 0, 1, 8), 0);

	/* addr = in_reg ? rega : stkp (branchless select)
	 *   addr = stkp ^ ((rega ^ stkp) & mask)
	 *   mask = -1 -> rega, mask = 0 -> stkp */
	MVal *diff = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, diff, rega, stkp);
	MVal *sel = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_AND, MT_PTR, sel, diff, mask);
	MVal *addr = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, addr, stkp, sel);

	/* dst = *addr */
	mout_addr(o, MMOP_LOAD, in->dtype, dst, maddr(addr, 0, 1, 0), 0);

	/* advance: reg path bumps the offset field by oinc (reg_save_area
	 * slot width: 8 GP / 16 FP); overflow path bumps overflow_arg_area
	 * by the SysV stack slot width (8 for all classes — x86_64 passes
	 * every stack argument in an 8-byte slot).  in_reg -> mask is -1. */
	MVal *incr = tmp(fm, MT_I64, "va");
	mout_cst(o, MMOP_AND, MT_I64, incr, mask, imm(fm, MT_I64, oinc));
	MVal *nof = tmp(fm, MT_I64, "va");
	mout(o, MMOP_ADD, MT_I64, nof, off64, incr);
	mout_addr(o, MMOP_STORE, MT_I32, 0, maddr(ap, 0, 1, ooff), nof);
	MVal *nstk = tmp(fm, MT_I64, "va");
	mout(o, MMOP_NOT, MT_I64, nstk, mask, 0);
	mout_cst(o, MMOP_AND, MT_I64, nstk, nstk, imm(fm, MT_I64, 8));
	MVal *nsp = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, nsp, stkp, nstk);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 8), nsp);
}

/* ---- main entry ----------------------------------------------------------- */

/* Copy instructions from b->ins[start..nins) into `o`, lowering
 * ARG/CALL/VA runs.  `o` is the block's output stream, pre-filled by
 * mabi_selpar for the entry block. */
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
		o->ins[o->nins - 1].extra = in->extra;   /* phi-edge marker */
	}
	free(args);

	if (b->term.op == MMOP_RET && (b->term.src[0] || b->term.td))
			mabi_selret(fm, o, &b->term);
}

void
mfnm_abi_x86_64(MFnM *fm)
{
	/* lower the entry block first: selpar computes fm->vafa which the
	 * varargs vastart expansion in later blocks depends on */
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
