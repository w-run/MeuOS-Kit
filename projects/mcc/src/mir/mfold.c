/* mfold.c — MIR constant folding + algebraic simplification (MIR_PASS_FOLD).
 *
 * Extracted from passes.c during the MIR backend file split (2026-08-07).
 * Operates on MFn (the platform-neutral MIR) independent of the existing
 * QBE-derived LIR passes.
 *
 * Folding model: fold*() mirror the existing fold.c semantics but take
 * MConst/MRef and produce an MConst; the driver (msimp_block) rewrites the
 * block instruction array, replacing folded instructions with a copy of the
 * folded constant and dropping dead instructions.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* -Os/-Oz: size-oriented codegen.  Defined here (rather than main.c) so
 * check-mir-* unit tests link it; ir.h has the extern declaration, the
 * driver sets it for -Os/-Oz. */
int g_opt_size;

/* -Ofast: fast-math folding gate (same standalone-link rationale). */
int g_fast_math;

/* Aggressive folding level gate: when nonzero, applies strength-reduction
 * rules (mul-by-power-of-2 -> shift).  Set by run_mir_passes for -O3 / -Os. */
int g_mir_fold_aggressive;

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
	if (op == MOP_UI2F && cl && cl->kind == MC_INT) {
		res->kind = MC_FLT;
		res->type = MT_F64;
		res->u.d = (cl->type == MT_I32)
		           ? (double)(uint32_t)cl->u.i
		           : (double)(uint64_t)cl->u.i;
		return 0;
	}
	if (op == MOP_F2I && cl && cl->kind == MC_FLT) {
		res->kind = MC_INT;
		if (w) {
			res->type = MT_I64;
			if (cl->type == MT_F32)
				res->u.i = (int64_t)(double)cl->u.s;
			else
				res->u.i = (int64_t)cl->u.d;
		} else {
			res->type = MT_I32;
			if (cl->type == MT_F32)
				res->u.i = (int32_t)cl->u.s;
			else
				res->u.i = (int32_t)cl->u.d;
		}
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
 * only safe when every use stays within the same block. */
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

/* Check whether a value is defined outside the given block. */
static bool
defined_outside(MFn *fn, MVal *v, MBlk *b)
{
	(void)fn;
	if (!v || v->kind != MV_TEMP)
		return false;
	if (v->defblk)
		return v->defblk != b;
	return v->def != 0;
}

/* ---- -Ofast fast-math algebraic simplification --------------------------
 * Applies only when g_fast_math is set and the operation's dtype is
 * floating point, assuming no NaN/Inf/signed-zero semantics. */
static MRef
mfast_simp(MFn *fn, MBlk *b, MOP op, MType dt, MRef a0, MRef a1)
{
	double dv;

	if (!g_fast_math)
		return (MRef){0};
	if (dt != MT_F32 && dt != MT_F64)
		return (MRef){0};

	if (a1.con && a1.con->kind == MC_FLT) {
		dv = a1.con->type == MT_F32 ? a1.con->u.s : a1.con->u.d;
		if ((op == MOP_ADD || op == MOP_SUB) && dv == 0.0)
			return a0;
		if (op == MOP_MUL && dv == 1.0)
			return a0;
		if (op == MOP_MUL && dv == 0.0)
			return MREF_CON(mconst_flt(fn, dt, 0.0));
		if (op == MOP_DIV && dv == 1.0)
			return a0;
	}
	if (a0.con && a0.con->kind == MC_FLT) {
		dv = a0.con->type == MT_F32 ? a0.con->u.s : a0.con->u.d;
		if (op == MOP_ADD && dv == 0.0)
			return a1;
		if (op == MOP_MUL && dv == 1.0)
			return a1;
		if (op == MOP_MUL && dv == 0.0)
			return MREF_CON(mconst_flt(fn, dt, 0.0));
	}
	if (a0.val && a1.val) {
		if (a0.val == a1.val) {
			if (op == MOP_SUB)
				return MREF_CON(mconst_flt(fn, dt, 0.0));
			if (op == MOP_DIV)
				return MREF_CON(mconst_flt(fn, dt, 1.0));
		} else if (op == MOP_SUB || op == MOP_DIV) {
			MIns *d0 = a0.val->def, *d1 = a1.val->def;
			if (d0 && d1 && d0 != d1 && d0->op == MOP_LOAD &&
			    d1->op == MOP_LOAD &&
			    d0->src[0].val == d1->src[0].val &&
			    d0 >= b->ins && d0 < b->ins + b->nins &&
			    d1 >= b->ins && d1 < b->ins + b->nins) {
				int i0 = (int)(d0 - b->ins), i1 = (int)(d1 - b->ins);
				if (i1 == i0 + 1 || i0 == i1 + 1) {
					if (op == MOP_SUB)
						return MREF_CON(mconst_flt(fn, dt, 0.0));
					return MREF_CON(mconst_flt(fn, dt, 1.0));
				}
			}
		}
	}
	if (op == MOP_NEG && a0.val && a0.val->def &&
	    a0.val->def->op == MOP_NEG &&
	    a0.val->def->dst == a0.val)
		return a0.val->def->src[0];

	return (MRef){0};
}

/* ---- memory constant propagation (block-local) --------------------------- */

typedef struct MMemC {
	MVal *addr;      /* slot address temp (MOP_ALLOCA result) */
	MConst *val;     /* constant stored */
	MType dtype;     /* store dtype (load must match) */
} MMemC;

static const MMemC *
memc_find(MMemC *mc, uint32_t n, MVal *addr)
{
	for (uint32_t i = 0; i < n; i++)
		if (mc[i].addr == addr)
			return &mc[i];
	return 0;
}

static void
memc_set(MMemC *mc, uint32_t *n, MVal *addr, MConst *val, MType dt)
{
	for (uint32_t i = 0; i < *n; i++) {
		if (mc[i].addr == addr) {
			mc[i].val = val;
			mc[i].dtype = dt;
			return;
		}
	}
	if (*n < 64) {
		mc[*n].addr = addr;
		mc[*n].val = val;
		mc[*n].dtype = dt;
		(*n)++;
	}
}

static MRef
memc_const(MFn *fn, MType dt, MConst *c)
{
	if (c->kind == MC_INT) {
		int w = dt == MT_I64 ? 8 : dt == MT_I32 ? 4 :
		        dt == MT_I16 ? 2 : 1;
		uint64_t mask = w >= 8 ? ~0ull : ((1ull << (8 * w)) - 1);
		return MREF_CON(mconst_int(fn, dt, c->u.i & mask));
	}
	if (c->kind == MC_FLT)
		return MREF_CON(c);
	return (MRef){0};
}

/* Rewrite a block: apply constant folding and algebraic simplification,
 * dropping instructions whose result is unused or simplified away.
 * Returns the number of instructions removed. */
static uint32_t
msimp_block(MFn *fn, MBlk *b, const char *is_alloca)
{
	MSimpMap *tab[32] = {0};
	MMemC memc[64];
	uint32_t nmemc = 0;
	MIns *out = b->ins;
	uint32_t nout = 0;
	uint32_t removed = 0;

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		MRef a0 = in->src[0], a1 = in->src[1];

		a0 = map_resolve(tab, a0);
		a1 = map_resolve(tab, a1);

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
			} else if (in->op == MOP_I2F || in->op == MOP_UI2F ||
			           in->op == MOP_F2I) {
				MConst *z = mconst_int(fn, MT_I64, 0);
				if (mfold_const(&cr, in->op, w, cl, z) == 0)
					folded = true;
			} else {
				if (cright && mfold_const(&cr, in->op, w, cl, cright) == 0)
					folded = true;
			}
			if (folded) {
				if ((in->op == MOP_I2F || in->op == MOP_UI2F) &&
				    cr.kind == MC_FLT && in->dtype == MT_F32) {
					cr.u.s = (float)cr.u.d;
					cr.type = MT_F32;
				}
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

		if (in->op == MOP_STORE) {
			MRef va = map_resolve(tab, in->src[0]);
			MRef aa = map_resolve(tab, in->src[1]);
			MVal *addr = aa.val;
			if (va.con && addr && addr->kind == MV_TEMP &&
			    is_alloca[addr->id] && !(in->extra & 2))
				memc_set(memc, &nmemc, addr, va.con, in->dtype);
			else
				nmemc = 0;
		} else if (in->op == MOP_LOAD) {
			MRef aa = map_resolve(tab, in->src[0]);
			MVal *addr = aa.val;
			const MMemC *mc = addr && addr->kind == MV_TEMP
				? memc_find(memc, nmemc, addr) : 0;
			if (mc && mc->dtype == in->dtype && !(in->extra & 2) &&
			    !used_outside(fn, in->dst, b)) {
				MRef fr = memc_const(fn, in->dtype, mc->val);
				if (fr.val || fr.con) {
					map_set(tab, in->dst, fr);
					removed++;
					continue;
				}
			}
		} else if (in->op == MOP_CALL) {
			nmemc = 0;
		}

		if (in->dst && in->op < MOP_JMP && !used_outside(fn, in->dst, b)) {
			MRef fastr = mfast_simp(fn, b, in->op, in->dtype, a0, a1);
			if (fastr.val || fastr.con) {
				map_set(tab, in->dst, fastr);
				removed++;
				continue;
			}
		}

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
				if (g_mir_fold_aggressive && a1.con &&
				    a1.con->kind == MC_INT && a1.con->u.i > 0) {
					uint64_t v = (uint64_t)a1.con->u.i;
					if ((v & (v - 1)) == 0) {
						int n = 0;
						while (!(v & 1)) {
							v >>= 1;
							n++;
						}
						in->op = MOP_SHL;
						in->src[0] = a0;
						in->src[1] = MREF_CON(mconst_int(fn, MT_I32, n));
						a1 = in->src[1];
					}
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
			case MOP_SHL:
			case MOP_SHR:
			case MOP_SAR:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				break;
			case MOP_UDIV:
			case MOP_UREM:
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i > 0) {
					uint64_t v = (uint64_t)a1.con->u.i;
					if ((v & (v - 1)) == 0) {
						uint64_t orig = v;
						int n = 0;
						while (!(v & 1)) {
							v >>= 1;
							n++;
						}
					MRef sh = MREF_CON(mconst_int(fn, MT_I32, n));
						if (in->op == MOP_UDIV) {
							in->op = MOP_SHR;
							in->src[0] = a0;
							in->src[1] = sh;
							a1 = sh;
						} else {
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

		MIns *o = &out[nout++];
		*o = *in;
		o->src[0] = a0;
		o->src[1] = a1;
	}

	b->term.src[0] = map_resolve(tab, b->term.src[0]);

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

/* ---- signed div/rem by power-of-two strength reduction (msdiv_pow2) ----- */

static MIns *
msdiv_emit(MIns *out, uint32_t n, MBlk *b, MOP op, MType dt, MVal *dst,
           MRef a0, MRef a1)
{
	MIns *in = &out[n];
	memset(in, 0, sizeof *in);
	in->op = op;
	in->dtype = dt;
	in->dst = dst;
	in->src[0] = a0;
	in->src[1] = a1;
	in->blk = b;
	in->id = n;
	if (dst && dst->kind == MV_TEMP) {
		dst->def = in;
		dst->defblk = b;
	}
	return in;
}

static bool
msdiv_pow2_block(MFn *fn, MBlk *b)
{
	bool changed = false;
	MIns *old = b->ins;
	uint32_t nold = b->nins;
	MIns *out = malloc((nold + 16) * sizeof *out);
	uint32_t nout = 0;

	for (uint32_t i = 0; i < nold; i++) {
		MIns *in = &old[i];
		MVal *d = in->dst;
		MRef a0 = in->src[0], a1 = in->src[1];
		bool isdiv = in->op == MOP_DIV || in->op == MOP_UDIV;
		bool isrem = in->op == MOP_REM || in->op == MOP_UREM;
		if (d && (isdiv || isrem) && a1.con &&
		    a1.con->kind == MC_INT && a1.con->u.i > 0 &&
		    (in->op == MOP_DIV || in->op == MOP_REM)) {
			uint64_t v = (uint64_t)a1.con->u.i;
			if ((v & (v - 1)) == 0 && v > 1) {
				int n = 0;
				uint64_t t = v;
				while (!(t & 1)) {
					t >>= 1;
					n++;
				}
				int bits = (in->dtype == MT_I64) ? 63 : 31;
				MType dt = in->dtype;
				MVal *t1 = mval_new(fn, MV_TEMP, dt, 0, "sdiv");
				MVal *t2 = mval_new(fn, MV_TEMP, dt, 0, "sdiv");
				MVal *t3 = mval_new(fn, MV_TEMP, dt, 0, "sdiv");
				msdiv_emit(out, nout++, b, MOP_SAR, dt, t1, a0,
				    MREF_CON(mconst_int(fn, MT_I32, bits)));
				msdiv_emit(out, nout++, b, MOP_AND, dt, t2, MREF_VAL(t1),
				    MREF_CON(mconst_int(fn, dt, (int64_t)(v - 1))));
				msdiv_emit(out, nout++, b, MOP_ADD, dt, t3, a0,
				    MREF_VAL(t2));
				if (isrem) {
					MVal *t4 = mval_new(fn, MV_TEMP, dt, 0, "sdiv");
					msdiv_emit(out, nout, b, MOP_SAR, dt, t4,
					    MREF_VAL(t3),
					    MREF_CON(mconst_int(fn, MT_I32, n)));
					nout++;
					msdiv_emit(out, nout, b, MOP_SHL, dt, t4,
					    MREF_VAL(t4),
					    MREF_CON(mconst_int(fn, MT_I32, n)));
					nout++;
					msdiv_emit(out, nout, b, MOP_SUB, dt, d, a0,
					    MREF_VAL(t4));
				} else {
					msdiv_emit(out, nout, b, MOP_SAR, dt, d,
					    MREF_VAL(t3),
					    MREF_CON(mconst_int(fn, MT_I32, n)));
				}
				nout++;
				changed = true;
				continue;
			}
		}
		out[nout++] = *in;
	}
	if (changed) {
		b->ins = out;
		b->nins = nout;
		b->cins = nout;
	} else {
		free(out);
	}
	return changed;
}

uint32_t
msdiv_pow2(MFn *fn)
{
	uint32_t r = 0;
	for (MBlk *b = fn->link; b; b = b->link)
		if (msdiv_pow2_block(fn, b))
			r++;
	return r;
}

/* ---- fold pass entry point -------------------------------------------- */

uint32_t
mfold(MFn *fn)
{
	uint32_t r = 0;
	build_uses(fn);
	char *is_alloca = calloc(fn->nval ? fn->nval : 1, 1);
	for (MBlk *b = fn->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++)
			if (b->ins[i].op == MOP_ALLOCA && b->ins[i].dst)
				is_alloca[b->ins[i].dst->id] = 1;
	for (MBlk *b = fn->link; b; b = b->link)
		r += msimp_block(fn, b, is_alloca);
	free(is_alloca);
	return r;
}