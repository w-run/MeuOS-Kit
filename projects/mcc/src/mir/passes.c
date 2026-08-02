/* passes.c — MIR optimization passes (B.2, first batch).
 *
 * Operates on MFn (the platform-neutral MIR) independent of the existing
 * QBE-derived LIR passes.  First batch:
 *   - build_uses:  populate MVal.use chains (SSA use-def)
 *   - mfold:       constant folding (int/float/addr) across instructions
 *   - msimpl:      algebraic simplification (x+0, x*1, x&0, x|0, x^x,
 *                  div/rem by power of 2 -> shift/and, ...)
 *   - mdce:        dead code elimination driven by use chains
 *
 * The pass pipeline is `run_mir_passes(fn)`; the C frontend wires into it
 * during B.4.  Until then these are validated by constructing MFn in the
 * mir test harness.
 *
 * Folding model: fold*() mirror the existing fold.c semantics but take
 * MConst/MRef and produce an MConst; the driver (mfold_block) rewrites the
 * block instruction array, replacing folded instructions with a copy of the
 * folded constant and dropping dead instructions.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* ---- use chain construction ------------------------------------------- */

static void
mark_use(MFn *fn, MVal *v, MIns *in, int argn)
{
	if (!v || v->kind != MV_TEMP)
		return;
	if (v->nuse == v->cuse) {
		v->cuse = v->cuse ? v->cuse * 2 : 4;
		v->use = realloc(v->use, v->cuse * sizeof *v->use);
	}
	v->use[v->nuse].ins = in;
	v->use[v->nuse].phi = 0;
	v->use[v->nuse].argn = argn;
	v->nuse++;
}

static void
build_uses_block(MFn *fn, MBlk *b)
{
	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		if (in->src[0].val)
			mark_use(fn, in->src[0].val, in, 0);
		if (in->src[1].val)
			mark_use(fn, in->src[1].val, in, 1);
	}
	if (b->term.src[0].val)
		mark_use(fn, b->term.src[0].val, &b->term, 0);
	for (MPhi *p = b->phi; p; p = p->link)
		for (uint32_t i = 0; i < p->narg; i++) {
			MVal *v = p->arg[i];
			if (!v || v->kind != MV_TEMP)
				continue;
			if (v->nuse == v->cuse) {
				v->cuse = v->cuse ? v->cuse * 2 : 4;
				v->use = realloc(v->use, v->cuse * sizeof *v->use);
			}
			v->use[v->nuse].ins = 0;
			v->use[v->nuse].phi = p;
			v->use[v->nuse].argn = -1;
			v->nuse++;
		}
}

static void
reset_uses(MFn *fn)
{
	for (uint32_t i = 0; i < fn->nval; i++)
		fn->val[i]->nuse = 0;
}

void
build_uses(MFn *fn)
{
	reset_uses(fn);
	for (MBlk *b = fn->link; b; b = b->link)
		build_uses_block(fn, b);
}

/* ---- constant helpers --------------------------------------------------- */

static int
con_is_bits(MConst *c, int *w)
{
	if (!c)
		return 0;
	if (c->kind == MC_INT) {
		*w = (c->type == MT_I64 || c->type == MT_PTR) ? 1 : 0;
		return 1;
	}
	return 0;
}

/* Fold `op` over constants cl/cr.  Returns 0 on success (res filled) or
 * 1 if not foldable.  `w` selects 64-bit arithmetic. */
static int
mfold_const(MConst *res, MOP op, int w, MConst *cl, MConst *cr)
{
	uint64_t x;
	int64_t ls, rs;
	uint64_t lu, ru;

	/* single-operand int<->float conversions (before the integer branch,
	 * which would otherwise intercept I2F since its source is MC_INT). */
	if (op == MOP_I2F && cl && cl->kind == MC_INT) {
		res->kind = MC_FLT;
		res->type = MT_F64;
		res->u.d = (double)cl->u.i;
		return 0;
	}
	if (op == MOP_F2I && cl && cl->kind == MC_FLT) {
		res->kind = MC_INT;
		res->type = MT_I32;
		if (cl->type == MT_F32)
			res->u.i = (int32_t)cl->u.s;
		else
			res->u.i = (int32_t)cl->u.d;
		return 0;
	}
	if (op == MOP_NEG && cl && cl->kind == MC_FLT) {
		res->kind = MC_FLT;
		res->type = cl->type;
		if (cl->type == MT_F32)
			res->u.s = -cl->u.s;
		else
			res->u.d = -cl->u.d;
		return 0;
	}

	if (op >= MOP_CEQ && op <= MOP_CFGE) {
		/* comparisons produce i32 0/1 */
		int eq, lt, gt;
		if (cl->kind == MC_FLT || cr->kind == MC_FLT) {
			double dl, dr;
			if (cl->kind == MC_FLT && cr->kind == MC_FLT) {
				dl = cl->type == MT_F32 ? cl->u.s : cl->u.d;
				dr = cr->type == MT_F32 ? cr->u.s : cr->u.d;
			} else {
				return 1;
			}
			switch (op) {
			case MOP_CFEQ: x = dl == dr; break;
			case MOP_CFNE: x = dl != dr; break;
			case MOP_CFLT: x = dl < dr;  break;
			case MOP_CFLE: x = dl <= dr; break;
			case MOP_CFGT: x = dl > dr;  break;
			case MOP_CFGE: x = dl >= dr; break;
			default: return 1;
			}
			res->kind = MC_INT;
			res->type = MT_I32;
			res->u.i = (int64_t)x;
			return 0;
		} else {
			if (!con_is_bits(cl, &w) || !con_is_bits(cr, &w))
				return 1;
			ls = cl->u.i; rs = cr->u.i;
			lu = (uint64_t)ls; ru = (uint64_t)rs;
			switch (op) {
			case MOP_CEQ: eq = lu == ru; lt = 0; gt = 0; break;
			case MOP_CNE: eq = lu != ru; lt = 0; gt = 0; break;
			case MOP_CSLT: eq = 0; lt = ls < rs; gt = 0; break;
			case MOP_CSLE: eq = 0; lt = ls <= rs; gt = 0; break;
			case MOP_CSGT: eq = 0; lt = 0; gt = ls > rs; break;
			case MOP_CSGE: eq = 0; lt = 0; gt = ls >= rs; break;
			case MOP_CULT: eq = 0; lt = lu < ru; gt = 0; break;
			case MOP_CULE: eq = 0; lt = lu <= ru; gt = 0; break;
			case MOP_CUGT: eq = 0; lt = 0; gt = lu > ru; break;
			case MOP_CUGE: eq = 0; lt = 0; gt = lu >= ru; break;
			default: return 1;
			}
			x = eq ? 1 : (lt ? 1 : (gt ? 1 : 0));
			res->kind = MC_INT;
			res->type = MT_I32;
			res->u.i = (op == MOP_CEQ) ? (lu == ru) :
			           (op == MOP_CNE) ? (lu != ru) :
			           (op == MOP_CSLT) ? (ls < rs) :
			           (op == MOP_CSLE) ? (ls <= rs) :
			           (op == MOP_CSGT) ? (ls > rs) :
			           (op == MOP_CSGE) ? (ls >= rs) :
			           (op == MOP_CULT) ? (lu < ru) :
			           (op == MOP_CULE) ? (lu <= ru) :
			           (op == MOP_CUGT) ? (lu > ru) :
			           (op == MOP_CUGE) ? (lu >= ru) : 0;
			return 0;
		}
	}

	if (cl->kind == MC_INT && cr->kind == MC_INT) {
		ls = cl->u.i; rs = cr->u.i;
		lu = (uint64_t)ls; ru = (uint64_t)rs;
		switch (op) {
		case MOP_ADD:  x = lu + ru; break;
		case MOP_SUB:  x = lu - ru; break;
		case MOP_MUL:  x = lu * ru; break;
		case MOP_DIV:
			if (ru == 0) return 1;
			if (rs == -1 && ls == (w ? INT64_MIN : INT32_MIN)) return 1;
			x = w ? ls / rs : (int32_t)ls / (int32_t)rs; break;
		case MOP_UDIV:
			if (ru == 0) return 1;
			x = w ? lu / ru : (uint32_t)lu / (uint32_t)ru; break;
		case MOP_REM:
			if (ru == 0) return 1;
			x = w ? ls % rs : (int32_t)ls % (int32_t)rs; break;
		case MOP_UREM:
			if (ru == 0) return 1;
			x = w ? lu % ru : (uint32_t)lu % (uint32_t)ru; break;
		case MOP_AND:  x = lu & ru; break;
		case MOP_OR:   x = lu | ru; break;
		case MOP_XOR:  x = lu ^ ru; break;
		case MOP_SHL:  x = lu << (ru & (w ? 63 : 31)); break;
		case MOP_SHR:  x = (w ? lu : (uint32_t)lu) >> (ru & (w ? 63 : 31)); break;
		case MOP_SAR:  x = (w ? ls : (int32_t)ls) >> (ru & (w ? 63 : 31)); break;
		case MOP_NEG:  x = -lu; break;
		case MOP_SEXT:
			x = w ? (int64_t)lu :
			    (cl->type == MT_I8 ? (int8_t)lu :
			     cl->type == MT_I16 ? (int16_t)lu : (int32_t)lu); break;
		case MOP_ZEXT: x = lu; break;
		case MOP_TRUNC: x = lu; break;
		default: return 1;
		}
		res->kind = MC_INT;
		res->type = w ? MT_I64 : MT_I32;
		res->u.i = (int64_t)x;
		if (!w)
			res->u.i = (int32_t)x;
		return 0;
	}

	return 1;
}

/* ---- algebraic simplification (msimpl) ---------------------------------- */

typedef struct MSimpMap {
	MVal *key;
	MRef repl;     /* replacement ref (val or con) */
	struct MSimpMap *next;
} MSimpMap;

static MSimpMap *
map_get(MSimpMap **tab, MVal *v)
{
	for (MSimpMap *m = tab[v->id & 31]; m; m = m->next)
		if (m->key == v)
			return m;
	return 0;
}

static void
map_set(MSimpMap **tab, MVal *v, MRef repl)
{
	MSimpMap *m = calloc(1, sizeof *m);
	m->key = v;
	m->repl = repl;
	m->next = tab[v->id & 31];
	tab[v->id & 31] = m;
}

/* resolve a ref through the simplification map */
static MRef
map_resolve(MSimpMap **tab, MRef r)
{
	if (r.val) {
		MSimpMap *m = map_get(tab, r.val);
		if (m) {
			if (m->repl.val)
				return m->repl;
			if (m->repl.con)
				return MREF_CON(m->repl.con);
		}
	}
	return r;
}

/* Check whether a value is used outside the given block (including by a
 * phi).  Folding/simplification that removes the defining instruction is
 * only safe when every use stays within the same block — otherwise the
 * value's uses in other blocks would become undefined. */
static bool
used_outside(MFn *fn, MVal *v, MBlk *b)
{
	(void)fn;
	if (!v || v->kind != MV_TEMP)
		return false;
	for (uint32_t i = 0; i < v->nuse; i++) {
		MUse *u = &v->use[i];
		if (u->phi)
			return true;
		if (u->ins && u->ins->blk != b)
			return true;
	}
	return false;
}

/* Check whether a value is defined outside the given block.  Folding an
 * instruction whose operand comes from another block would substitute a
 * cross-block value into this block's computation, changing the SSA
 * dependency structure (promote/ssa then sees a load reading a cross-block
 * alloca result with no phi). */
static bool
defined_outside(MFn *fn, MVal *v, MBlk *b)
{
	(void)fn;
	if (!v || v->kind != MV_TEMP)
		return false;
	if (v->defblk)
		return v->defblk != b;
	/* no def block recorded: conservatively treat as cross-block */
	return v->def != 0;
}

/* Rewrite a block: apply constant folding and algebraic simplification,
 * dropping instructions whose result is unused or simplified away.
 * Returns the number of instructions removed. */
static uint32_t
msimp_block(MFn *fn, MBlk *b)
{
	MSimpMap *tab[32] = {0};
	MIns *out = b->ins;
	uint32_t nout = 0;
	uint32_t removed = 0;

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		MRef a0 = in->src[0], a1 = in->src[1];

		/* resolve operands through the replacement map */
		a0 = map_resolve(tab, a0);
		a1 = map_resolve(tab, a1);

		/* constant folding (skip if dst is used outside this block or as
		 * an alloca size) */
		if (in->dst && a0.con && (in->op < MOP_JMP || in->op > MOP_CALL) &&
		    !used_outside(fn, in->dst, b) &&
		    true) {
			int w = (in->dtype == MT_I64 || in->dtype == MT_PTR);
			MConst cr;
			MConst *cl = a0.con;
			MConst *cright = a1.con;
			bool folded = false;
			MRef fr;

			if (in->op == MOP_NEG) {
				MConst *z = mconst_int(fn, cl->type, 0);
				if (mfold_const(&cr, MOP_NEG, w, cl, z) == 0)
					folded = true;
			} else if (in->op == MOP_I2F || in->op == MOP_F2I) {
				/* single-operand conversions */
				MConst *z = mconst_int(fn, MT_I64, 0);
				if (mfold_const(&cr, in->op, w, cl, z) == 0)
					folded = true;
			} else {
				/* Both operands must be constant to fold.  Treating a
				 * missing right operand as 0 would wrongly collapse
				 * e.g. `or c, load` into `or c, 0 = c`, dropping a
				 * memory read (bitfield stores broke exactly this
				 * way). */
				if (cright && mfold_const(&cr, in->op, w, cl, cright) == 0)
					folded = true;
			}
			if (folded) {
				/* I2F folding produces a double; if the MIR destination
				 * is f32, narrow the folded constant accordingly. */
				if (in->op == MOP_I2F && cr.kind == MC_FLT &&
				    in->dtype == MT_F32) {
					cr.u.s = (float)cr.u.d;
					cr.type = MT_F32;
				}
				/* folded: map dst -> constant */
				if (cr.kind == MC_ADDR)
					fr = MREF_CON(mconst_addr(fn, cr.u.addr.sym, cr.u.addr.off,
					                         cr.u.addr.tls, cr.u.addr.isext));
				else if (cr.kind == MC_FLT)
					fr = MREF_CON(mconst_flt(fn, cr.type, cr.type == MT_F32 ? cr.u.s : cr.u.d));
				else
					fr = MREF_CON(mconst_int(fn, cr.type, cr.u.i));
				map_set(tab, in->dst, fr);
				removed++;
				continue;
			}
		}

		/* algebraic simplifications on non-constant operands.  Only when
		 * the result is used in-block AND the operands are also defined
		 * in-block: substituting a cross-block operand would change the
		 * SSA dependency structure for promote/ssa. */
		if (in->dst && in->op < MOP_JMP &&
		    !used_outside(fn, in->dst, b) &&
		    		    !defined_outside(fn, a0.val, b) &&
		    !defined_outside(fn, a1.val, b)) {
			switch (in->op) {
			case MOP_ADD:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				break;
			case MOP_MUL:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 1) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, MREF_CON(mconst_int(fn, in->dtype, 0)));
					removed++;
					continue;
				}
				break;
			case MOP_AND:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, MREF_CON(mconst_int(fn, in->dtype, 0)));
					removed++;
					continue;
				}
				break;
			case MOP_OR:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				break;
			case MOP_XOR:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				if (a0.val && a1.val && a0.val == a1.val) {
					map_set(tab, in->dst, MREF_CON(mconst_int(fn, in->dtype, 0)));
					removed++;
					continue;
				}
				break;
			case MOP_SUB:
				if (a0.val && a1.val && a0.val == a1.val) {
					map_set(tab, in->dst, MREF_CON(mconst_int(fn, in->dtype, 0)));
					removed++;
					continue;
				}
				break;
			case MOP_UDIV:
			case MOP_UREM:
			case MOP_DIV:
			case MOP_REM:
				/* div/rem by power-of-two -> shift/and (unsigned only) */
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i > 0) {
					uint64_t v = (uint64_t)a1.con->u.i;
					if ((v & (v - 1)) == 0) {
						uint64_t orig = v; /* keep the divisor for the rem mask */
						/* portable ctzll (mcc has no __builtin_*) */
						int n = 0;
						while (!(v & 1)) {
							v >>= 1;
							n++;
						}
					MRef sh = MREF_CON(mconst_int(fn, MT_I32, n));
						if (in->op == MOP_UDIV || in->op == MOP_DIV) {
							/* rewrite in place to a shift so the definition stays
							 * at the original position (SSA order preserved; madd
							 * would move the def past its uses).  a1 must track
							 * the new shift amount: the copy at the end of the
							 * loop rewrites src[1] from a1, and a stale a1 would
							 * clobber the rewritten shift amount back to the
							 * divisor (x/8 turned into x>>8 instead of x>>3). */
							MOP shf = (in->op == MOP_UDIV) ? MOP_SHR : MOP_SAR;
							in->op = shf;
							in->src[0] = a0;
							in->src[1] = sh;
							a1 = sh;
						} else {
							/* rem by 2^n -> and (n-1), in place.  v was
							 * consumed by the ctz loop above (64 -> 1), so
							 * build the mask from the original divisor:
							 * `x % 64` must become `x & 63`, not `x & 0`
							 * (which made BSet's bsiter clear only bit 0
							 * and loop forever on any multi-bit set). */
							MRef msk = MREF_CON(mconst_int(fn, in->dtype, orig - 1));
							in->op = MOP_AND;
							in->src[0] = a0;
							in->src[1] = msk;
							a1 = msk;
						}
					}
				}
				break;
			default:
				break;
			}
		}

		/* keep instruction, rewriting operands through the map */
		MIns *o = &out[nout++];
		*o = *in;
		o->src[0] = a0;
		o->src[1] = a1;
	}

	/* resolve the terminator through the map before freeing it */
	b->term.src[0] = map_resolve(tab, b->term.src[0]);

	/* free the map */
	for (int k = 0; k < 32; k++) {
		MSimpMap *m = tab[k];
		while (m) {
			MSimpMap *next = m->next;
			free(m);
			m = next;
		}
	}

	b->nins = nout;
	return removed;
}

/* ---- dead code elimination (mdce) -------------------------------------- */

static uint32_t
mdce_block(MFn *fn, MBlk *b)
{
	(void)fn;
	MIns *out = b->ins;
	uint32_t nout = 0;
	uint32_t removed = 0;

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		bool has_side_effect = (in->op == MOP_STORE || in->op == MOP_CALL ||
		                        in->op == MOP_ALLOCA || in->op == MOP_VASTART ||
		                        in->op == MOP_SALLOC);
		if (in->dst && in->dst->nuse == 0 && !has_side_effect &&
		    in->dst->kind == MV_TEMP) {
			/* no uses and no side effects -> dead */
			removed++;
			continue;
		}
		out[nout++] = *in;
	}
	b->nins = nout;
	return removed;
}

/* ---- pass pipeline ----------------------------------------------------- */

uint32_t
run_mir_pass(MFn *fn, enum MIRPass pass)
{
	switch (pass) {
	case MIR_PASS_USES:
		build_uses(fn);
		return 0;
	case MIR_PASS_FOLD: {
		uint32_t r = 0;
		build_uses(fn); /* used_outside needs the use chains */
		for (MBlk *b = fn->link; b; b = b->link)
			r += msimp_block(fn, b);
		return r;
	}
	case MIR_PASS_COPY:
		return mcopy(fn);
	case MIR_PASS_GVN:
		return mgvn(fn);
	case MIR_PASS_DCE: {
		uint32_t r = 0;
		build_uses(fn);
		for (MBlk *b = fn->link; b; b = b->link)
			r += mdce_block(fn, b);
		return r;
	}
	case MIR_PASS_SSA:
		return mssa_check(fn);
	default:
		return 0;
	}
}

void
run_mir_passes(MFn *fn, int optlevel)
{
	if (optlevel < 1)
		return;
	/* B.2 pipeline: fold/simpl first so GVN sees canonical forms, then
	 * copy propagation (forward copies so GVN keys are stable), then
	 * global value numbering, then dead code elimination. */
	run_mir_pass(fn, MIR_PASS_FOLD);
	run_mir_pass(fn, MIR_PASS_COPY);
	run_mir_pass(fn, MIR_PASS_GVN);
	run_mir_pass(fn, MIR_PASS_DCE);
	/* B.6 验收项 2: explicit-SSA consistency gate after the pipeline. */
	if (mssa_check(fn))
		fprintf(stderr, "mcc: %s: SSA consistency check FAILED\n", fn->name);
}
