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

/* ---- ARM AAPCS aggregate classification -------------------------------- */

/* Simplified AAPCS classification: an aggregate ≤ 4 bytes fits in a GPR
 * (INTEGER); larger aggregates are passed on the stack (or via sret for
 * returns).  No eightbyte splitting like x86_64 SysV. */
typedef struct MAClass {
	MTypeDesc *td;
	int inmem;              /* 0 = in reg, 1 = stack param / sret */
	uint32_t size;          /* size rounded up to 4 */
	int align;              /* log2 alignment (2 = 4, 3 = 8) */
} MAClass;

static void
mabi_typclass(MAClass *a, MTypeDesc *td)
{
	uint32_t sz = td->size;
	int32_t al = td->align < 4 ? 4 : td->align;
	sz = (sz + al - 1) & ~(al - 1);
	a->td = td;
	a->size = sz;
	a->align = al == 8 ? 3 : 2;
	if (td->is_incomplete || sz > 4) {
		a->inmem = 1;
	} else {
		a->inmem = 0;
	}
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
	MAClass aret;
	MAClass *pa = 0;

	if (fm->retty) {
		mabi_typclass(&aret, fm->retty);
		if (aret.inmem) {
			/* sret: hidden pointer in r0.  Stash it in an alloca slot
			 * since r0 is caller-saved and may be clobbered by body calls. */
			fm->has_sret = true;
			MVal *r0 = rarg(fm, &ni, &ns, false);
			MVal *pad = tmp(fm, MT_PTR, "sret");
			mout(o, MMOP_ALLOCA16, MT_PTR, pad, 0, 0);
			mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(pad, 0, 1, 0), r0);
			fm->sret_pad = pad;
		}
		pa = &aret;
	}

	for (int i = 0; i < n; i++) {
		MInsM *p = &parms[i];
		MVal *dst = p->dst;
		bool isf = p->dtype == MT_F32 || p->dtype == MT_F64;

		if (p->td) {
			MAClass ac;
			mabi_typclass(&ac, p->td);
			if (ac.inmem) {
				/* caller pushed the aggregate; dst addresses it via fp */
				mout_addr(o, MMOP_LEA, MT_PTR, dst,
				          maddr(reg(fm, ARM_R11), 0, 1, off), 0);
				off += ac.size;
				continue;
			}
			/* register-passed aggregate (≤ 4 bytes): copy from rN into
			 * the pad addressed by dst */
			mout(o, MMOP_ALLOCA16, MT_PTR, dst, 0, 0);
			MVal *r0 = rarg(fm, &ni, &ns, false);
			if (r0)
				mout_addr(o, MMOP_STORE, MT_I32, 0,
				          maddr(dst, 0, 1, 0), r0);
			continue;
		}

		if (p->dtype == MT_I64) {
			/* AAPCS: i64 needs even-aligned register pair.
			 * If ni is odd, skip one register. */
			if (ni < 4 && (ni & 1))
				ni++;
			if (ni + 1 < 4) {
				/* In registers r0:r1 or r2:r3.  A single
				 * MMOP_MOV (MT_I64) lets the emitter copy both
				 * halves into dst's frame slot after regalloc —
				 * pre-splitting into i32 temps hard-wires
				 * dst->slot (still -1 here, mabi runs before
				 * regalloc) and the halves land in garbage. */
				int lo_reg = mt->argreg[0 + ni];
				ni += 2;
				mout(o, MMOP_MOV, MT_I64, dst,
				     reg(fm, lo_reg), 0);
			} else {
				/* From stack: a single MT_I64 load; emit_load
				 * writes both halves into dst's slot. */
				mout_addr(o, MMOP_LOAD, MT_I64, dst,
				          maddr(reg(fm, ARM_R11), 0, 1, off), 0);
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

	/* varargs: save r0-r3 to the stack so va_start can find them */
	if (fm->host && fm->host->vararg) {
		MVal *fp = reg(fm, ARM_R11);
		/* save r0-r3 at fp-16..fp-4 (below the pushed {r11, lr}) */
		for (int i = 0; i < 4; i++)
			mout_addr(o, MMOP_STORE, MT_I32, 0,
			          maddr(fp, 0, 1, -16 + i * 4),
			          reg(fm, mt->argreg[0 + i]));
	}

	/* record varargs register usage for selvastart */
	(void)pa;
	*vafa = (ni << 4) | (ns << 8) | (off << 12);
}

/* ---- selcall: call lowering -------------------------------------------- */

static void
mabi_selcall(MFnM *fm, MOut *o, MInsM *args, int n, MInsM *call)
{
	const MTargetM *mt = fm->mt;
	MAClass *ac = calloc(n, sizeof *ac);
	MAClass aret, *pa = 0;

	if (call->td) {
		mabi_typclass(&aret, call->td);
		pa = &aret;
	}

	/* classify each argument */
	int ni = 0, ns = 0, stk = 0;
	for (int i = 0; i < n; i++) {
		if (args[i].td) {
			mabi_typclass(&ac[i], args[i].td);
			if (ac[i].inmem) {
				stk += ac[i].size;
			} else {
				ni++;  /* register: uses one GPR */
			}
		} else if (args[i].dtype == MT_I64) {
			if (ni < 4 && (ni & 1)) ni++;
			if (ni + 1 < 4)
				ni += 2;
			else
				stk += 8;
		} else if (call->extra &&
		           (args[i].dtype == MT_F64 || args[i].dtype == MT_F32)) {
			/* Variadic FP: AAPCS base standard — pass via GPRs
			 * (F64=2 words, F32=1 word) or stack if no room.
			 * The VFP→GPR bridge stages the value through the SALLOC
			 * area at [SP+soff]; that area must be reserved even when
			 * the argument lands in registers (soff is not consumed
			 * there), or [SP] aliases the frame's first slot — the
			 * va_list alloca — and the bridge store clobbers it
			 * (arm-varargs: double vararg before the next va_arg
			 * read garbage gp_offset). */
			if (args[i].dtype == MT_F64) {
				if (ni < 4 && (ni & 1)) ni++;
				if (ni + 1 < 4) {
					ni += 2;
					stk += 8;   /* bridge scratch */
				} else {
					stk += 8;   /* stack arg itself */
				}
			} else {
				if (ni >= 4) {
					stk += 8;   /* stack arg itself */
				} else {
					ni++;
					stk += 8;   /* bridge scratch */
				}
			}
		} else {
			bool isf = args[i].dtype == MT_F32 || args[i].dtype == MT_F64;
			if (isf ? ns >= 8 : ni >= 4)
				stk += 8;
			else
				{ if (isf) ns++; else ni++; }
		}
	}
	stk = (stk + 7) & ~7;
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, ARM_SP),
		         imm(fm, MT_I64, stk));

	/* aggregate return: sret hidden pointer in r0 */
	if (pa) {
		if (aret.inmem) {
			MVal *pad = tmp(fm, MT_PTR, "abi");
			mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
			         mconst_int(fm->host, MT_I32, aret.size));
			mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
			MVal *r0 = rarg(fm, &ni, &ns, false);
			mout(o, MMOP_MOV, MT_PTR, r0, call->dst, 0);
		} else {
			/* ≤ 4 byte aggregate return: reserve a pad, point dst at it */
			MVal *pad = tmp(fm, MT_PTR, "abi");
			mout_cst(o, MMOP_ALLOCA16, MT_PTR, pad, 0,
			         mconst_int(fm->host, MT_I32, aret.size));
			mout(o, MMOP_MOV, MT_PTR, call->dst, pad, 0);
		}
	}

	ni = 0; ns = 0;
	int soff = 0;
	for (int i = 0; i < n; i++) {
		MInsM *a = &args[i];
		if (a->td) {
			if (ac[i].inmem) {
				/* stack: blit the aggregate onto the stack */
				MVal *dstp = tmp(fm, MT_PTR, "abi");
				mout_addr(o, MMOP_LEA, MT_PTR, dstp,
				          maddr(reg(fm, ARM_SP), 0, 1, soff), 0);
				mout_blit(fm, o, dstp, a->src[0], ac[i].size);
				soff += ac[i].size;
			} else {
				/* register (≤ 4 bytes): load from pointer into rN */
				MVal *r0 = rarg(fm, &ni, &ns, false);
				if (r0)
					mout_addr(o, MMOP_LOAD, MT_I32, r0,
					          maddr(a->src[0], 0, 1, 0), 0);
			}
			continue;
		}
		bool isf = a->dtype == MT_F32 || a->dtype == MT_F64;
		if (call->extra && isf) {
			/* Variadic FP (base standard): pass via GPRs/stack
			 * using the SALLOC area as bridge for VFP→GPR move. */
			if (a->dtype == MT_F64) {
				/* Store to SALLOC area, re-load as two I32 */
				if (ni < 4 && (ni & 1)) ni++;
				if (ni + 1 < 4) {
					mout_addr(o, MMOP_STORE, MT_F64, 0,
					          maddr(reg(fm, ARM_SP), 0, 1, soff), a->src[0]);
					int lo = mt->argreg[0 + ni];
					int hi = mt->argreg[0 + ni + 1];
					ni += 2;
					mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, lo),
					          maddr(reg(fm, ARM_SP), 0, 1, soff), 0);
					mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, hi),
					          maddr(reg(fm, ARM_SP), 0, 1, soff + 4), 0);
				} else {
					mout_addr(o, MMOP_STORE, MT_F64, 0,
					          maddr(reg(fm, ARM_SP), 0, 1, soff), a->src[0]);
					soff += 8;
				}
			} else {
				/* F32: store to SALLOC, re-load as I32 */
				if (ni < 4) {
					mout_addr(o, MMOP_STORE, MT_F32, 0,
					          maddr(reg(fm, ARM_SP), 0, 1, soff), a->src[0]);
					mout_addr(o, MMOP_LOAD, MT_I32,
					          reg(fm, mt->argreg[0 + ni]),
					          maddr(reg(fm, ARM_SP), 0, 1, soff), 0);
					ni++;
				} else {
					mout_addr(o, MMOP_STORE, MT_F32, 0,
					          maddr(reg(fm, ARM_SP), 0, 1, soff), a->src[0]);
					soff += 8;
				}
			}
			continue;
		}
		if (a->dtype == MT_I64) {
			/* I64 value: load two i32 halves.  MV_CONST (literal
			 * constant like a long long function argument) has
			 * no slot; materialize the halves directly. */
			if (ni < 4 && (ni & 1)) ni++;
			if (ni + 1 < 4) {
				int lo_reg = mt->argreg[0 + ni];
				int hi_reg = mt->argreg[0 + ni + 1];
				ni += 2;
				if (a->src[0]->kind == MV_CONST &&
				    a->src[0]->con) {
					int64_t cv = a->src[0]->con->u.i;
					mout(o, MMOP_MOV, MT_I32,
					     reg(fm, lo_reg),
					     mval_const(fm->host, MT_I32,
					                imm(fm, MT_I32,
					                    (int32_t)cv)), 0);
					mout(o, MMOP_MOV, MT_I32,
					     reg(fm, hi_reg),
					     mval_const(fm->host, MT_I32,
					                imm(fm, MT_I32,
					                    (int32_t)(cv>>32))),0);
				} else {
					int slot = a->src[0]->slot;
					mout_addr(o, MMOP_LOAD, MT_I32,
					          reg(fm, lo_reg),
					          maddr(reg(fm, ARM_R11), 0,
					                1, slot), 0);
					mout_addr(o, MMOP_LOAD, MT_I32,
					          reg(fm, hi_reg),
					          maddr(reg(fm, ARM_R11), 0,
					                1, slot + 4), 0);
				}
			} else {
				if (a->src[0]->kind == MV_CONST &&
				    a->src[0]->con) {
					int64_t cv = a->src[0]->con->u.i;
					mout_addr(o, MMOP_STORE, MT_I32, 0,
					          maddr(reg(fm, ARM_SP), 0,
					                1, soff),
					          mval_const(fm->host, MT_I32,
					                     imm(fm, MT_I32,
					                         (int32_t)cv)));
					mout_addr(o, MMOP_STORE, MT_I32, 0,
					          maddr(reg(fm, ARM_SP), 0,
					                1, soff + 4),
					          mval_const(fm->host, MT_I32,
					                     imm(fm, MT_I32,
					                         (int32_t)(cv>>32))));
				} else {
					int slot = a->src[0]->slot;
					mout_addr(o, MMOP_LOAD, MT_I32,
					          tmp(fm, MT_I32, "i64tmp"),
					          maddr(reg(fm, ARM_R11), 0,
					                1, slot), 0);
					MVal *tmpv = o->ins[o->nins-1].dst;
					mout_addr(o, MMOP_STORE, MT_I32, 0,
					          maddr(reg(fm, ARM_SP), 0,
					                1, soff), tmpv);
					mout_addr(o, MMOP_LOAD, MT_I32,
					          tmp(fm, MT_I32, "i64tmp"),
					          maddr(reg(fm, ARM_R11), 0,
					                1, slot+4), 0);
					MVal *tmpv2 = o->ins[o->nins-1].dst;
					mout_addr(o, MMOP_STORE, MT_I32, 0,
					          maddr(reg(fm, ARM_SP), 0,
					                1, soff+4), tmpv2);
				}
				soff += 8;
			}
			continue;
		}
		if (isf ? ns >= 8 : ni >= 4) {
			mout_addr(o, MMOP_STORE, a->dtype, 0,
			          maddr(reg(fm, ARM_SP), 0, 1, soff),
			          a->src[0]);
			soff += 8;
			continue;
		}
		mout(o, MMOP_MOV, a->dtype, rarg(fm, &ni, &ns, isf), a->src[0], 0);
	}

	/* the call itself */
	mout(o, MMOP_CALL, call->dtype, call->dst, call->src[0], 0);
	o->ins[o->nins - 1].td = call->td;

	/* caller cleanup: restore stack-argument space */
	if (stk)
		mout_cst(o, MMOP_SALLOC, MT_PTR, 0, reg(fm, ARM_SP),
		         imm(fm, MT_I64, -(int64_t)stk));

	/* results: aggregate return handling */
	if (pa) {
		if (aret.inmem) {
			/* sret: RAX returns the pad pointer */
			mout(o, MMOP_MOV, MT_PTR, call->dst, reg(fm, ARM_R0), 0);
		} else {
			/* ≤ 4B register return: store r0 into the pad */
			mout_addr(o, MMOP_STORE, MT_I32, 0,
			          maddr(call->dst, 0, 1, 0), reg(fm, ARM_R0));
		}
	}
	free(ac);
}

/* ---- selret: return lowering ------------------------------------------- */

static void
mabi_selret(MFnM *fm, MOut *o, MInsM *term)
{
	bool ret64 = false;
	if (term->td) {
		MAClass aret;
		mabi_typclass(&aret, term->td);
		if (!term->src[0]) {
			/* dead path: nothing to pack */
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
		if (aret.inmem) {
			/* sret: copy the aggregate into the hidden return buffer.
			 * R0 may have been clobbered by body calls, so reload the
			 * pad address from the alloca slot stashed by selpar. */
			if (fm->sret_pad)
				mout_addr(o, MMOP_LOAD, MT_PTR, reg(fm, ARM_R0),
				          maddr(fm->sret_pad, 0, 1, 0), 0);
			mout_blit(fm, o, reg(fm, ARM_R0), term->src[0], aret.size);
			mout(o, MMOP_MOV, MT_PTR, reg(fm, ARM_R0), reg(fm, ARM_R0), 0);
		} else {
			/* ≤ 4B register return: load from source pointer into r0 */
			mout_addr(o, MMOP_LOAD, MT_I32, reg(fm, ARM_R0),
			          maddr(term->src[0], 0, 1, 0), 0);
		}
	} else {
		MVal *s0 = term->src[0];
		if (!s0) {
			term->op = MMOP_RET;
			term->src[0] = 0;
			term->td = 0;
			return;
		}
	bool isf = s0->type == MT_F32 || s0->type == MT_F64;
	if (s0->type == MT_I64) {
		/* i64 return: r0 (low 32) + r1 (high 32). */
		if (s0->kind == MV_CONST && s0->con) {
			/* Immediate constant return (e.g. `return 42`): materialize
			 * the low/high 32-bit halves directly as arm i32 moves, the
			 * same way the i386 backend does.  A bare `ret (i64)42` has
			 * no stack slot; reading s0->slot would emit `ldr r0,[fp+N]`
			 * off the uninitialized frame and return garbage. */
			int64_t cv = s0->con->u.i;
			mout(o, MMOP_MOV, MT_I32, reg(fm, ARM_R0),
			     mval_const(fm->host, MT_I32,
			                imm(fm, MT_I32, (int32_t)cv)), 0);
			mout(o, MMOP_MOV, MT_I32, reg(fm, ARM_R1),
			     mval_const(fm->host, MT_I32,
			                imm(fm, MT_I32, (int32_t)(cv >> 32))), 0);
		} else {
			/* Slot-resident value: leave s0 in the term so the emitter's
			 * MMOP_RET loads both halves after regalloc (s0->slot is -1
			 * here — mabi runs before regalloc — so pre-splitting into
			 * r0/r1 loads would read the uninitialized frame, the same
			 * trap the i386 backend documents for x86-i64param). */
			ret64 = true;
		}
	} else {
			MVal *rreg = isf ? reg(fm, ARM_D0) : reg(fm, ARM_R0);
			mout(o, MMOP_MOV, s0->type, rreg, s0, 0);
		}
	}
	term->op = MMOP_RET;
	if (!ret64)
		term->src[0] = 0;
	term->td = 0;
}

/* ---- varargs ------------------------------------------------------------- */

/* ARM AAPCS varargs: va_list structure (16 bytes on ARM, 4-byte ptrs):
 *   offset 0: gp_offset  (uint32_t) — offset in reg_save_area (0..16)
 *   offset 4: fp_offset  (uint32_t) — offset in fp_reg_save_area (0..64)
 *   offset 8: overflow_arg_area (void *) — pointer to stack arguments
 *   offset 12: reg_save_area (void *) — pointer to saved r0-r3 area
 *
 * The register save area holds r0-r3 (16 bytes) at fp-16..fp-1 (saved
 * by selpar when the function is variadic).  Stack arguments start at
 * fp+8 (after the pushed {r11, lr}).
 */

static void
mabi_vastart(MFnM *fm, MOut *o, MVal *ap, uint32_t vafa)
{
	MVal *fp = reg(fm, ARM_R11);
	int gp = ((vafa >> 4) & 15) * 4;   /* GPRs consumed by named params */
	int sp = vafa >> 12;                /* first stack arg offset from fp */

	/* gp_offset = gp (bytes consumed in reg_save_area) */
	{
		MVal *gpc = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, gp));
		mout_addr(o, MMOP_STORE, MT_I32, 0, maddr(ap, 0, 1, 0), gpc);
	}
	/* fp_offset = 0 (no FP register save area) */
	{
		MVal *fpv = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, 0));
		mout_addr(o, MMOP_STORE, MT_I32, 0, maddr(ap, 0, 1, 4), fpv);
	}
	/* overflow_arg_area = fp + sp */
	MVal *oa = tmp(fm, MT_PTR, "abi");
	mout_addr(o, MMOP_LEA, MT_PTR, oa, maddr(fp, 0, 1, sp), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 8), oa);
	/* reg_save_area = fp - 16 */
	MVal *rs = tmp(fm, MT_PTR, "abi");
	mout_addr(o, MMOP_LEA, MT_PTR, rs, maddr(fp, 0, 1, -16), 0);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 12), rs);
}

static void
mabi_vaarg(MFnM *fm, MOut *o, MInsM *in)
{
	MVal *ap = in->src[0];
	MVal *dst = in->dst;
	/* On ARM, variadic float args are passed as integers (AAPCS rule),
	 * so we always use gp_offset (offset 0).  The advance per argument is
	 * 4 for 32-bit types and 8 for 64-bit types (register pair / stack
	 * double-word).  Hardcoding 4 across all types breaks double and
	 * long long — the gp_offset does not advance far enough and the
	 * overflow_arg_area under-reads. */
	int ooff = 0;          /* gp_offset field offset in va_list */
	int oinc = (in->dtype == MT_I64 || in->dtype == MT_F64) ? 8 : 4;
	int limit = 16;        /* reg_save_area size: 4 × 4 = 16 bytes */

	/* offset = gp_offset, aligned up to 8 for 64-bit types (AAPCS
	 * requires even register pairs for double/long long: a lone named
	 * GPR leaves gp_offset odd, and the next 64-bit vararg lands in the
	 * pair after a skip).  The alignment applies to the in-register
	 * test and the advance as well, so the next va_arg sees a correct
	 * gp_offset. */
	MVal *off = tmp(fm, MT_I32, "va");
	mout_addr(o, MMOP_LOAD, MT_I32, off, maddr(ap, 0, 1, ooff), 0);
	if (oinc == 8) {
		MVal *a7 = mval_const(fm->host, MT_I32,
		                      imm(fm, MT_I32, 7));
		MVal *t = tmp(fm, MT_I32, "va");
		mout(o, MMOP_ADD, MT_I32, t, off, a7);
		MVal *m8 = mval_const(fm->host, MT_I32,
		                      imm(fm, MT_I32, (int32_t)-8));
		MVal *al = tmp(fm, MT_I32, "va");
		mout(o, MMOP_AND, MT_I32, al, t, m8);
		off = al;
	}
	/* in_reg = offset < limit (unsigned) */
	{
		MVal *limv = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, limit));
		mout(o, MMOP_CMP, MT_I32, 0, off, limv);
	}
	MVal *inr = tmp(fm, MT_I32, "va");
	MInsM *sc = mout(o, MMOP_SETCC, MT_I32, inr, 0, 0);
	sc->cc = MCC_CC;
	/* mask = in_reg ? -1 : 0 (32-bit) */
	MVal *mask = tmp(fm, MT_I32, "va");
	mout(o, MMOP_NEG, MT_I32, mask, inr, 0);

	/* reg path address = reg_save_area + offset */
	MVal *regp = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, regp, maddr(ap, 0, 1, 12), 0);
	MVal *rega = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, rega, regp, off);
	/* overflow path address = overflow_arg_area */
	MVal *stkp = tmp(fm, MT_PTR, "va");
	mout_addr(o, MMOP_LOAD, MT_PTR, stkp, maddr(ap, 0, 1, 8), 0);

	/* addr = in_reg ? rega : stkp (branchless select via mask) */
	MVal *diff = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, diff, rega, stkp);
	MVal *sel = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_AND, MT_PTR, sel, diff, mask);
	MVal *addr = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_XOR, MT_PTR, addr, stkp, sel);

	/* dst = *addr */
	mout_addr(o, MMOP_LOAD, in->dtype, dst, maddr(addr, 0, 1, 0), 0);

	/* advance: reg path bumps gp_offset by oinc */
	{
		MVal *incv = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, oinc));
		MVal *incr = tmp(fm, MT_I32, "va");
		mout(o, MMOP_AND, MT_I32, incr, mask, incv);
		MVal *nof = tmp(fm, MT_I32, "va");
		mout(o, MMOP_ADD, MT_I32, nof, off, incr);
		mout_addr(o, MMOP_STORE, MT_I32, 0, maddr(ap, 0, 1, ooff), nof);
	}
	/* overflow path: advance overflow_arg_area by 8 always.
	 * ARM AAPCS vararg caller (selcall) reserves 8 bytes per stack
	 * argument (soff += 8) for all types, so the overflow area
	 * pointer must advance by 8 regardless of the argument type.
	 * Using argument size (oinc=4 for int) would under-advance
	 * and misread the next argument. */
	MVal *nstk = tmp(fm, MT_I32, "va");
	mout(o, MMOP_NOT, MT_I32, nstk, mask, 0);
	{
		MVal *oiv = mval_const(fm->host, MT_I32,
		                       imm(fm, MT_I32, 8));
		mout(o, MMOP_AND, MT_I32, nstk, nstk, oiv);
	}
	MVal *nsp = tmp(fm, MT_PTR, "va");
	mout(o, MMOP_ADD, MT_PTR, nsp, stkp, nstk);
	mout_addr(o, MMOP_STORE, MT_PTR, 0, maddr(ap, 0, 1, 8), nsp);
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
