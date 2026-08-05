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

/* -Os/-Oz: size-oriented codegen。定义在本文件（而非 main.c）是因为
 * check-mir-* 单元测试把 passes.c 单独链接成 mir_test，main.c 不在
 * 链接范围内；ir.h 中 extern 声明，driver 在 -Os/-Oz 时置位。 */
int g_opt_size;

/* -Ofast: fast-math 折叠门控（同样定义在本文件以支持 mir_test 独立
 * 链接）。driver 在 -Ofast 时置位，msimp_block 据此应用无 NaN/Inf/
 * 符号零语义的代数恒等式折叠。 */
int g_fast_math;

/* Aggressive folding level gate: when nonzero, msimp_block applies
 * strength-reduction rules that -O2 keeps disabled (e.g. mul-by-power-of-2
 * -> shift, which shrinks code: imul is 7 bytes, shl is 4).  Set by
 * run_mir_passes for -O3 and -Os/-Oz (optlevel >= 3 || g_opt_size). */
int g_mir_fold_aggressive;

/* -dP / --opt-log: when nonzero, run_mir_passes prints a per-pass line for
 * each optimization pass recording how many transformations it performed
 * (folds, simplifications, copies propagated, GVN numbers, dead insns
 * eliminated, ...).  Declared in ir.h; the driver sets it for -dP. */
int g_opt_log;

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
			/* 64-bit destination: keep the full range (long)2e15
			 * must fold to 2000000000000000, not truncate to int32. */
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

/* ---- -Ofast fast-math algebraic simplification --------------------------
 * Applies only when g_fast_math is set and the operation's dtype is
 * floating point, assuming no NaN/Inf/signed-zero semantics:
 *   x + 0.0 -> x, 0.0 + x -> x, x - 0.0 -> x
 *   x * 1.0 -> x, 1.0 * x -> x, x * 0.0 -> 0.0, 0.0 * x -> 0.0
 *   x / 1.0 -> x, x / x -> 1.0 (x != 0), x - x -> 0.0
 *   -(-x) -> x
 * Returns a replacement ref or a null MRef when nothing applies. */
static MRef
mfast_simp(MFn *fn, MBlk *b, MOP op, MType dt, MRef a0, MRef a1)
{
	double dv;

	if (!g_fast_math)
		return (MRef){0};
	if (dt != MT_F32 && dt != MT_F64)
		return (MRef){0};

	/* right-operand constant */
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
	/* left-operand constant */
	if (a0.con && a0.con->kind == MC_FLT) {
		dv = a0.con->type == MT_F32 ? a0.con->u.s : a0.con->u.d;
		if (op == MOP_ADD && dv == 0.0)
			return a1;
		if (op == MOP_MUL && dv == 1.0)
			return a1;
		if (op == MOP_MUL && dv == 0.0)
			return MREF_CON(mconst_flt(fn, dt, 0.0));
	}
	/* x - x -> 0.0, x / x -> 1.0: same SSA value, or two adjacent loads
	 * of the same slot (a local read twice — no store can intervene
	 * between adjacent loads, so the two reads are equal) */
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
	/* -(-x) -> x */
	if (op == MOP_NEG && a0.val && a0.val->def &&
	    a0.val->def->op == MOP_NEG &&
	    a0.val->def->dst == a0.val)
		return a0.val->def->src[0];

	return (MRef){0};
}

/* ---- memory constant propagation (block-local) ---------------------------
 * Forward a constant store to a stack slot (MOP_ALLOCA result) into a later
 * load from the same slot address within the same block — the C pattern
 * `int k = 7; ...; use(k)` where k lives on its slot.  Conservative rules:
 *   - only MOP_ALLOCA-derived addresses are tracked: two distinct alloca
 *     temps can never alias, so a store to one does not affect another;
 *     a store through any other (derived/unknown) pointer could alias a
 *     tracked slot, so it invalidates the whole table;
 *   - a call may write through an escaped slot pointer -> invalidate all;
 *   - loads never invalidate (reads do not modify memory);
 *   - volatile stores are rejected in the frontend (funcstore), so they
 *     cannot reach here; atomic accesses use dedicated atomic ops, not
 *     plain MOP_LOAD/MOP_STORE. */
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

/* Constant value actually held by a slot after a width-dtype store:
 * the stored constant is masked to the store width (the slot holds the
 * low bits; a same-width load re-reads them). */
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
	/* MC_ADDR pointer constants are not folded (relocatable semantics). */
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
			} else if (in->op == MOP_I2F || in->op == MOP_UI2F ||
			           in->op == MOP_F2I) {
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
				if ((in->op == MOP_I2F || in->op == MOP_UI2F) &&
				    cr.kind == MC_FLT && in->dtype == MT_F32) {
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

		/* memory constant propagation: forward a constant store into a
		 * same-block load of the same alloca slot.  src[0]=value and
		 * src[1]=address for MOP_STORE (the mir.h comment is stale; see
		 * x86_64_mbe.c funcstore convention).  Volatile accesses are
		 * marked in MIns.extra bit 1 (MIR_VOLATILE, set by func_to_mir
		 * from the frontend's INST_VOLATILE) and never folded. */
		if (in->op == MOP_STORE) {
			MRef va = map_resolve(tab, in->src[0]);
			MRef aa = map_resolve(tab, in->src[1]);
			MVal *addr = aa.val;
			if (va.con && addr && addr->kind == MV_TEMP &&
			    is_alloca[addr->id] && !(in->extra & 2))
				memc_set(memc, &nmemc, addr, va.con, in->dtype);
			else
				nmemc = 0;   /* unknown/volatile writer: could alias any slot */
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
			/* a load never invalidates (reads do not modify memory) */
		} else if (in->op == MOP_CALL) {
			nmemc = 0;   /* callee may write through an escaped slot */
		}

		/* algebraic simplifications on non-constant operands.  Only when
		 * the result is used in-block AND the operands are also defined
		 * in-block: substituting a cross-block operand would change the
		 * SSA dependency structure for promote/ssa. */
		/* -Ofast fast-math folding: the result is a copy or constant, so
		 * it is safe even when operands come from other blocks (e.g.
		 * function params) — the defined_outside SSA constraint below is
		 * for restructuring rules, not copies. */
		if (in->dst && in->op < MOP_JMP && !used_outside(fn, in->dst, b)) {
			MRef fastr = mfast_simp(fn, b, in->op, in->dtype, a0, a1);
			if (fastr.val || fastr.con) {
				map_set(tab, in->dst, fastr);
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
				/* -O3/-Os/-Oz 强度削减：x * 2^n -> x << n。乘法按 2 的
				 * 补码回绕，与移位逐位等价（有符号溢出本就是 UB）。
				 * imul $k（7 字节）换成 shl $n（4 字节），同时减体积。 */
				if (g_mir_fold_aggressive && a1.con &&
				    a1.con->kind == MC_INT && a1.con->u.i > 0) {
					uint64_t v = (uint64_t)a1.con->u.i;
					if ((v & (v - 1)) == 0) {
						int n = 0;
						while (!(v & 1)) {
							v >>= 1;
							n++;
						}
						/* in-place rewrite：SSA 顺序保持不变（移位
						 * 量常量必须在定义前已存在；madd 会把定义
						 * 移到使用之后）。 */
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
				/* shift by 0 is the identity: x << 0 == x >> 0 == x >>> 0 == x */
				if (a1.con && a1.con->kind == MC_INT && a1.con->u.i == 0) {
					map_set(tab, in->dst, a0);
					removed++;
					continue;
				}
				break;
			case MOP_UDIV:
			case MOP_UREM:
				/* div/rem by power-of-two -> shift/and (unsigned only).  The
				 * signed MOP_DIV/MOP_REM must NOT be strength-reduced here:
				 * C division rounds toward zero while SAR rounds toward
				 * -inf, so folding `-7/2` to SAR produced -4 instead of -3
				 * (verified 2026-08-03). */
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
						/* rewrite in place to a shift so the definition stays
						 * at the original position (SSA order preserved; madd
						 * would move the def past its uses).  a1 must track
						 * the new shift amount: the copy at the end of the
						 * loop rewrites src[1] from a1, and a stale a1 would
						 * clobber the rewritten shift amount back to the
						 * divisor (x/8 turned into x>>8 instead of x>>3). */
						if (in->op == MOP_UDIV) {
							in->op = MOP_SHR;
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

/* ---- signed div/rem by power-of-two strength reduction (msdiv_pow2) ----- */
/* -O3/-Os/-Oz: x / 2^n rounds toward zero while SAR rounds toward -inf, so
 * the unsigned shift/and fold does not apply.  Emit the classic
 * correction sequence:
 *   q = (x + ((x >> (bits-1)) & (2^n - 1))) >> n      (arithmetic)
 *   r = x - (q << n)
 * only when the divisor is a positive power of two. */

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
	in->id = n;   /* debug only */
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
					/* q = (x + corr) >> n; r = x - (q << n) */
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

static uint32_t
msdiv_pow2(MFn *fn)
{
	uint32_t r = 0;
	for (MBlk *b = fn->link; b; b = b->link)
		if (msdiv_pow2_block(fn, b))
			r++;
	return r;
}

/* ---- load forwarding + dead-store elimination (mloadfwd) ---------------- */
/* The frontend lowers every scalar into its variable slot (store) and
 * reloads it (load); store→load pairs for non-escaping local slots are
 * redundant.  Forwarding replaces the load with a copy of the stored value
 * (COPY propagates it), and dead-store elimination drops stores whose slot
 * is never read.  Together they let DCE collapse whole computation chains
 * the frontend routed through memory. */

typedef struct MLoadMap {
	MVal *addr;          /* slot address value */
	MVal *val;           /* last stored value */
	struct MLoadMap *next;
} MLoadMap;

static MLoadMap *
mloadfwd_get(MLoadMap *m, MVal *addr)
{
	for (; m; m = m->next)
		if (m->addr == addr)
			return m;
	return 0;
}

/* Does `v` refer to a unique non-escaping local slot?  Only DIRECT alloca
 * results qualify (tracked in the is_alloca bitmap computed up front —
 * the MVal.def pointers may be stale after prior passes removed
 * instructions): computed addresses (base+off for arrays/struct fields)
 * can alias one another, so forwarding/dead-store on them is unsound.
 * `v` must additionally not escape (every use is a load/store base). */
static bool
mloadfwd_slot(MFn *fn, const bool *is_alloca, MVal *v)
{
	if (!v || v->kind != MV_TEMP)
		return false;
	if (!is_alloca[v->id])
		return false;
	for (uint32_t i = 0; i < v->nuse; i++) {
		MUse *u = &v->use[i];
		if (u->phi || !u->ins)
			return false;
		MIns *in = u->ins;
		bool base = (in->op == MOP_LOAD && u->argn == 0) ||
		            (in->op == MOP_STORE && u->argn == 1);
		if (!base)
			return false;
	}
	return true;
}

uint32_t
mloadfwd(MFn *fn)
{
	uint32_t r = 0;
	bool *is_alloca = calloc(fn->nval ? fn->nval : 1, sizeof *is_alloca);
	for (MBlk *b = fn->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++)
			if (b->ins[i].op == MOP_ALLOCA && b->ins[i].dst)
				is_alloca[b->ins[i].dst->id] = true;

	/* pass 1: forward store→load within each block (unique slots) */
	for (MBlk *b = fn->link; b; b = b->link) {
		build_uses(fn);
		MLoadMap *map = 0;
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op == MOP_STORE) {
				MVal *addr = in->src[1].val;
				if (!mloadfwd_slot(fn, is_alloca, addr))
					continue;
				MLoadMap *e = mloadfwd_get(map, addr);
				if (!e) {
					e = calloc(1, sizeof *e);
					e->addr = addr;
					e->next = map;
					map = e;
				}
				e->val = in->src[0].val;
			} else if (in->op == MOP_LOAD) {
				MVal *addr = in->src[0].val;
				if (!mloadfwd_slot(fn, is_alloca, addr))
					continue;
				MLoadMap *e = mloadfwd_get(map, addr);
				if (e && e->val) {
					/* %dst = load %addr  ->  %dst = copy %val */
					in->op = MOP_COPY;
					in->src[0].val = e->val;
					in->src[0].con = 0;
					in->src[1].val = 0;
					in->src[1].con = 0;
					r++;
				}
			}
		}
		while (map) {
			MLoadMap *nx = map->next;
			free(map);
			map = nx;
		}
	}

	/* pass 2: dead stores — a store to a unique slot whose address value
	 * is used ONLY by stores (never loaded, never copied, never passed
	 * on) writes memory nothing reads.  All decisions are taken BEFORE
	 * any removal: shifting the block array invalidates the def pointers
	 * that mloadfwd_slot() relies on. */
	build_uses(fn);
	for (MBlk *b = fn->link; b; b = b->link) {
		bool *drop = calloc(b->nins ? b->nins : 1, sizeof *drop);
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op != MOP_STORE)
				continue;
			MVal *addr = in->src[1].val;
			if (!mloadfwd_slot(fn, is_alloca, addr))
				continue;
			bool only_stores = true;
			for (uint32_t j = 0; j < addr->nuse; j++) {
				MUse *u = &addr->use[j];
				if (!u->ins || u->ins->op != MOP_STORE ||
				    u->argn != 1) {
					only_stores = false;
					break;
				}
			}
			if (only_stores) {
				drop[i] = true;
				r++;
			}
		}
		MIns *out = b->ins;
		uint32_t nout = 0;
		for (uint32_t i = 0; i < b->nins; i++)
			if (!drop[i])
				out[nout++] = b->ins[i];
		b->nins = nout;
		free(drop);
	}
	free(is_alloca);
	build_uses(fn);
	return r;
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
		/* MOP_PAR is ABI-significant: even an unused parameter occupies an
		 * argument register slot, so the machine backend's selpar must see
		 * every one (removing an unused PAR shifted a following scalar
		 * param's register from RSI to RDI, breaking the caller/callee
		 * agreement under MCC_MIR_BACKEND=1; verified 2026-08-03). */
		bool has_side_effect = (in->op == MOP_STORE || in->op == MOP_CALL ||
		                        in->op == MOP_ALLOCA || in->op == MOP_VASTART ||
		                        in->op == MOP_SALLOC || in->op == MOP_PAR);
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

/* ---- if-conversion: constant-condition branch simplification (mifconv) ---- */

static uint32_t
mifconv(MFn *fn)
{
	uint32_t r = 0;
	for (MBlk *b = fn->link; b; b = b->link) {
		if (b->term.op != MOP_JNZ)
			continue;
		MRef cond = b->term.src[0];
		if (!cond.con)
			continue;
		/* constant condition: fold JNZ to simple JMP */
		if (cond.con->kind == MC_INT) {
			bool taken = cond.con->u.i != 0;
			b->term.op = MOP_JMP;
			b->term.src[0] = (MRef){0};
			if (taken) {
				b->s2 = 0;          /* s1 is the taken target */
			} else {
				b->s1 = b->s2;      /* s2 is the fallthrough (always taken) */
				b->s2 = 0;
			}
			r++;
		}
	}
	return r;
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
		/* Collect alloca-result temps.  The MVal.def instruction pointer
		 * is unreliable here: msimp_block compacts each block's ins array
		 * in place, so a def recorded in an already-visited block may
		 * point at overwritten storage.  Allocas are never removed by
		 * FOLD, so this function-wide id set stays valid for the whole
		 * pass. */
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
	case MIR_PASS_COPY:
		return mcopy(fn);
	case MIR_PASS_LOADFWD:
		return mloadfwd(fn);
	case MIR_PASS_MEM2REG:
		return mmem2reg(fn);
	case MIR_PASS_GVN:
		return mgvn(fn);
	case MIR_PASS_IFCONV:
		return mifconv(fn);
	case MIR_PASS_DCE: {
		/* iterate to fix-point: removing one dead instruction may leave
		 * its operands' defs dead too (e.g. a chain a=b+c; b=x after
		 * copy propagation) */
		uint32_t r = 0, round;
		do {
			build_uses(fn);
			round = 0;
			for (MBlk *b = fn->link; b; b = b->link)
				round += mdce_block(fn, b);
			r += round;
		} while (round);
		return r;
	}
	case MIR_PASS_COMBINE:
		return mcombine(fn);
	case MIR_PASS_SSA:
		return mssa_check(fn);
	default:
		return 0;
	}
}

/* -dP optimization log: human-readable per-pass name. */
static const char *
mir_pass_name(enum MIRPass pass)
{
	switch (pass) {
	case MIR_PASS_USES:    return "uses";
	case MIR_PASS_FOLD:    return "fold";
	case MIR_PASS_COPY:    return "copy";
	case MIR_PASS_LOADFWD: return "loadfwd";
	case MIR_PASS_MEM2REG: return "mem2reg";
	case MIR_PASS_GVN:     return "gvn";
	case MIR_PASS_IFCONV:  return "ifconv";
	case MIR_PASS_DCE:     return "dce";
	case MIR_PASS_COMBINE: return "combine";
	case MIR_PASS_SSA:     return "ssa";
	default:               return "?";
	}
}

/* Run a single pass; with g_opt_log (-dP) print one line recording how many
 * transformations it made (each pass already returns its change count). */
static uint32_t
run_plog(MFn *fn, enum MIRPass pass)
{
	uint32_t n = run_mir_pass(fn, pass);
	if (g_opt_log && n)
		fprintf(stderr, "  [%s] %s: %u change%s\n", fn->name,
		    mir_pass_name(pass), n, n == 1 ? "" : "s");
	return n;
}

void
run_mir_passes(MFn *fn, int optlevel)
{
	/* -O 级别语义分级（细化：与任务描述对齐）：
	 *   -O0: 仅 DCE（死代码消除）+ 基本 use chain 构建
	 *   -O1: 基础优化 — 常量折叠(mfold) + 代数简化(msimpl) + 复制传播(mcopy)
	 *        + 常量分支简化(mifconv)
	 *   -O2（默认）: 全局优化 — 在 -O1 基础上 + mem2reg(提升) + mloadfwd(加载
	 *        转发) + GVN(全局值编号) + 第二轮 ifconv + 第二轮 copy
	 *   -O3: 激进优化 — 在 -O2 基础上 + mcombine(指令合并) + 有符号 div/rem
	 *        强度削减(msdiv_pow2) + 第二轮折叠 + 激进强度削减(mul 2^n -> shift)
	 *   -Os/-Oz: 尺寸导向 — 同 -O2 但加 mifconv 第二轮折叠 + 激进强度削减
	 *   g_mir_fold_aggressive 按级别门控 msimp_block 的激进规则（mul 2^n -> shift
	 *   和 signed div/rem 强度削减）。 */
	uint32_t level = (uint32_t)(optlevel < 0 ? 0 : optlevel);
	g_mir_fold_aggressive = (level >= 3 || g_opt_size);
	if (g_opt_log)
		fprintf(stderr, "== opt-log: %s (optlevel %d) ==\n", fn->name, optlevel);

	if (level < 1) {
		/* -O0: minimal — build uses + DCE only */
		build_uses(fn);
		run_plog(fn, MIR_PASS_DCE);
		/* SSA consistency gate */
		if (mssa_check(fn))
			fprintf(stderr, "mcc: %s: SSA consistency check FAILED\n",
			        fn->name);
		return;
	}

	/* -O1: 基础优化管线 */
	run_plog(fn, MIR_PASS_FOLD);       /* 常量折叠 + 代数简化 */
	run_plog(fn, MIR_PASS_IFCONV);     /* 常量分支简化 */
	run_plog(fn, MIR_PASS_COPY);       /* 复制传播 */

	if (level >= 2) {
		/* -O2: 全局优化管线
		 * mem2reg first: promoting non-escaping scalar slots to SSA
		 * values kills the load/store traffic across basic blocks that
		 * LOADFWD (block-local) cannot reach — notably parameters and
		 * induction variables reloaded on every loop iteration.  LOADFWD
		 * then mops up the slots mem2reg conservatively declined. */
		run_plog(fn, MIR_PASS_MEM2REG);
		run_plog(fn, MIR_PASS_COPY);
		run_plog(fn, MIR_PASS_LOADFWD);
		run_plog(fn, MIR_PASS_GVN);
		run_plog(fn, MIR_PASS_IFCONV);  /* GVN may expose new constants */
		run_plog(fn, MIR_PASS_COPY);   /* propagate load-forwarded copies */

		/* -Os/-Oz: 尺寸导向 — 同 -O2 水准但加 mifconv 第二轮折叠
		 * （无需额外操作，ifconv 已在上方运行；保留 g_opt_size 标志
		 * 供后端 emit 做尺寸优先的指令选择）。 */
		if (level >= 3) {
			/* -O3: 激进优化
			 * 指令合并 + 有符号 div/rem 强度削减 + 第二轮折叠 */
			run_plog(fn, MIR_PASS_COMBINE);
			msdiv_pow2(fn);
			run_plog(fn, MIR_PASS_FOLD);
			run_plog(fn, MIR_PASS_IFCONV);
			run_plog(fn, MIR_PASS_COPY);
		}
		run_plog(fn, MIR_PASS_DCE);
	}
	/* B.6 验收项 2: explicit-SSA consistency gate after the pipeline. */
	if (mssa_check(fn))
		fprintf(stderr, "mcc: %s: SSA consistency check FAILED\n", fn->name);
}
