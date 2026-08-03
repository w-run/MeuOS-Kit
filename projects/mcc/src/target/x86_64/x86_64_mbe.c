/* x86_64_mbe.c — MIR machine backend entry (P2 parallel validation).
 *
 * Minimal MFn -> MFnM conversion (the seed of the P3 instruction
 * selector) plus the SysV ABI lowering.  In P2 this is a
 * parallel-validation hook: MCC_MIR_BACKEND=1 runs it on every function
 * (convert, ABI-lower, dump to stderr) while the bridge path keeps
 * producing the assembly.  P3-P5 fill in the real isel/regalloc/emit.
 *
 * The conversion reuses the bridge-decisions.md mapping rules (MOP/dtype
 * -> MMOP/width) and the aggregate markers (MV_TYPE encodes the typ[]
 * index, retty/paramty select the aggregate types).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "x86_64_m.h"

extern void mfnm_abi_x86_64(MFnM *fm);

/* forward-ordered block array (mf->link is reversed) */
static MBlk **
blk_order(MFn *mf, uint32_t *n)
{
	MBlk **o = calloc(mf->nblk, sizeof *o);
	uint32_t i = mf->nblk;
	for (MBlk *b = mf->link; b; b = b->link)
		o[--i] = b;
	*n = mf->nblk;
	return o;
}

static MMOP
map_op(MOP op, bool isf)
{
	switch (op) {
	case MOP_ADD:  return isf ? MMOP_FADD : MMOP_ADD;
	case MOP_SUB:  return isf ? MMOP_FSUB : MMOP_SUB;
	case MOP_MUL:  return isf ? MMOP_FMUL : MMOP_MUL;
	case MOP_DIV:  return isf ? MMOP_FDIV : MMOP_DIV;
	case MOP_NEG:  return isf ? MMOP_FNEG : MMOP_NEG;
	case MOP_REM:  return MMOP_REM;
	case MOP_UDIV: return MMOP_UDIV;
	case MOP_UREM: return MMOP_UREM;
	case MOP_AND:  return MMOP_AND;
	case MOP_OR:   return MMOP_OR;
	case MOP_XOR:  return MMOP_XOR;
	case MOP_SHL:  return MMOP_SHL;
	case MOP_SHR:  return MMOP_SHR;
	case MOP_SAR:  return MMOP_SAR;
	case MOP_COPY: return MMOP_MOV;
	case MOP_CAST: return MMOP_MOV;
	case MOP_SEXT: return MMOP_MOVSX;
	case MOP_ZEXT: return MMOP_MOVZX;
	case MOP_FEXT:   return MMOP_CVTSS2SD;   /* f32 -> f64 */
	case MOP_FTRUNC: return MMOP_CVTSD2SS;   /* f64 -> f32 */
	case MOP_ALLOCA: return MMOP_ALLOCA16;
	case MOP_SALLOC: return MMOP_SALLOC;
	case MOP_VASTART: return MMOP_VASTART;
	case MOP_VAARG:   return MMOP_VAARG;
	default:       return MMOP_NONE;
	}
}

/* MIR comparison op -> machine condition code (int + float, IEEE approx:
 * unordered NaN cases use the carry-clear forms like the LIR backend). */
static MCC
mir_cmp_cc(MOP op)
{
	switch (op) {
	case MOP_CEQ:  return MCC_EQ;
	case MOP_CNE:  return MCC_NE;
	case MOP_CSLT: return MCC_LT;
	case MOP_CSLE: return MCC_LE;
	case MOP_CSGT: return MCC_GT;
	case MOP_CSGE: return MCC_GE;
	case MOP_CULT: return MCC_CC;
	case MOP_CULE: return MCC_LS;
	case MOP_CUGT: return MCC_HI;
	case MOP_CUGE: return MCC_CS;
	case MOP_CFEQ: return MCC_EQ;
	case MOP_CFNE: return MCC_NE;
	case MOP_CFLT: return MCC_CC;
	case MOP_CFLE: return MCC_LS;
	case MOP_CFGT: return MCC_HI;
	case MOP_CFGE: return MCC_CS;
	default:       return MCC_EQ;
	}
}

/* F2I / I2F opcode by source/destination precision. */
static MMOP
mir_conv_op(MOP op, bool src64, bool dst64)
{
	switch (op) {
	case MOP_F2I:  return src64 ? MMOP_CVTTSD2SI : MMOP_CVTTSS2SI;
	case MOP_I2F:  return dst64 ? MMOP_CVTSI2SD : MMOP_CVTSI2SS;
	case MOP_UI2F: return dst64 ? MMOP_CVTSI2SD_U : MMOP_CVTSI2SS_U;
	default:       return MMOP_NONE;
	}
}

static MType
mref_type(MRef r)
{
	if (r.val)
		return r.val->type;
	if (r.con)
		return r.con->type;
	return MT_NONE;
}

static MVal *
mref_val(MRef r)
{
	return r.val ? r.val : 0;
}

/* MRef -> MVal, wrapping constants as MV_CONST values so the machine
 * layer can reference them as operands (maddm only takes MVal*). */
static MVal *
mval_of_ref(MFn *mf, MRef r)
{
	if (r.val)
		return r.val;
	if (r.con)
		return mval_const(mf, r.con->type, r.con);
	return 0;
}

/* P3b scope: scalar functions only.  Returns false (caller falls back to
 * the bridge path) when the function uses constructs the machine backend
 * does not yet lower.  Phase 1 (2026-08-03) closed all x86_64 fallbacks:
 * aggregate params/returns/args, SALLOC, TLS globals, dynamic alloca
 * (VLA) — MCC_MIR_BACKEND=1 is now the complete x86_64 path.  (A future
 * backend/feature that needs to fall back can re-add a check here.) */
static bool
mbe_supported(MFn *mf)
{
	(void)mf;
	return true;
}

bool
mfnm_backend_x86_64(MFn *mf)
{
	if (!mbe_supported(mf))
		return false;   /* caller falls back to the bridge path */
	MFnM *fm = mfnm_new(mf, &mtarget_x86_64, mf->name);
	fm->retty = mf->rettyd;   /* aggregate return type (P3a) */

	/* blocks in forward order */
	uint32_t nblk = 0;
	MBlk **order = blk_order(mf, &nblk);
	MBlkM **mblk = calloc(nblk, sizeof *mblk);
	for (uint32_t i = 0; i < nblk; i++) {
		mblk[i] = mblkm_new(fm, order[i]->name ? order[i]->name : "b");
		mfnm_addblk(fm, mblk[i]);
	}

	for (uint32_t bi = 0; bi < nblk; bi++) {
		MBlk *mb = order[bi];
		MBlkM *b = mblk[bi];

		for (uint32_t k = 0; k < mb->nins; k++) {
			MIns *in = &mb->ins[k];
			MVal *dst = in->dst;

			switch (in->op) {
			case MOP_PAR: {
				/* dst->td carries the aggregate MTypeDesc (P3a) */
				MInsM *mi = maddm(fm, b, MMOP_PARM, dst ? dst->type : MT_NONE,
				                  dst, 0, 0);
				mi->td = dst ? dst->td : 0;
				break;
			}
			case MOP_ARG: {
				MInsM *mi = maddm(fm, b, MMOP_ARG, in->dtype, 0,
				                  mval_of_ref(mf, in->src[0]), 0);
				if (in->src[0].val && in->src[0].val->kind == MV_TYPE) {
					/* aggregate: src[1] is the source pointer; td is
					 * the MV_TYPE's MTypeDesc (P3a) */
					mi->td = in->src[0].val->td;
					mi->src[0] = mval_of_ref(mf, in->src[1]);
				}
				break;
			}
			case MOP_CALL: {
				/* func_to_mir leaves the result MVal untyped; the regalloc
				 * needs it to pick the FPR pool for float returns. */
				if (dst && dst->type == MT_NONE)
					dst->type = in->dtype;
				MInsM *mi = maddm(fm, b, MMOP_CALL, in->dtype, dst,
				                  mval_of_ref(mf, in->src[0]), 0);
				if (in->src[1].val && in->src[1].val->kind == MV_TYPE)
					mi->td = in->src[1].val->td;
				break;
			}
			case MOP_LOAD: {
				maddm_addr(fm, b, MMOP_LOAD, in->dtype, dst,
				           maddr(mval_of_ref(mf, in->src[0]), 0, 1, 0), 0);
				break;
			}
			case MOP_STORE: {
				/* MIR convention (see funcstore): src[0] = value,
				 * src[1] = address (mir.h comment is stale) */
				maddm_addr(fm, b, MMOP_STORE, in->dtype, 0,
				           maddr(mval_of_ref(mf, in->src[1]), 0, 1, 0),
				           mval_of_ref(mf, in->src[0]));
				break;
			}
			case MOP_VASTART:
				maddm(fm, b, MMOP_VASTART, in->dtype, 0,
				      mval_of_ref(mf, in->src[0]), 0);
				break;
			case MOP_VAARG:
				maddm(fm, b, MMOP_VAARG, in->dtype, dst,
				      mval_of_ref(mf, in->src[0]), 0);
				break;
			case MOP_F2I:
			case MOP_I2F:
			case MOP_UI2F: {
				MType st = mref_type(in->src[0]);
				MType dt = in->dtype;
				MMOP co = mir_conv_op(in->op,
				                      st == MT_F64 || st == MT_I64,
				                      dt == MT_F64 || dt == MT_I64);
				maddm(fm, b, co, dt, dst, mval_of_ref(mf, in->src[0]), 0);
				break;
			}
			default: {
				if (in->op >= MOP_CEQ && in->op <= MOP_CFGE) {
					/* compare -> cmp + setcc */
					MVal *a0 = mval_of_ref(mf, in->src[0]);
					MVal *a1 = mval_of_ref(mf, in->src[1]);
					if (a0 && a1) {
						MInsM *ci = maddm(fm, b, MMOP_CMP, in->dtype, 0, a0, a1);
						(void)ci;
						maddm_cc(fm, b, MMOP_SETCC, in->dtype, dst, 0, 0,
						         mir_cmp_cc(in->op));
					}
					break;
				}
				MMOP mo = map_op(in->op,
				                in->dtype == MT_F32 || in->dtype == MT_F64);
				if (mo == MMOP_NONE)
					break;
				MInsM *mi = maddm(fm, b, mo, in->dtype, dst,
				                   mval_of_ref(mf, in->src[0]), mval_of_ref(mf, in->src[1]));
				if (in->cst)
					mi->cst = in->cst;
				break;
			}
			}
		}

		/* terminator */
		switch (mb->term.op) {
		case MOP_JMP:
			mfnm_term(fm, b, MMOP_JMP, 0, mblk[mb->s1->id], 0, MCC_NONE);
			break;
		case MOP_JNZ: {
			/* cond value vs 0 -> cmp + jcc ne */
			MVal *cond = mval_of_ref(mf, mb->term.src[0]);
			if (cond) {
				MConst *c0 = mconst_int(mf, cond->type, 0);
				MInsM *mi = maddm_cst(fm, b, MMOP_CMP, cond->type, 0, cond, c0);
				(void)mi;
			}
			mfnm_term(fm, b, MMOP_JCC, 0,
			          mb->s1 ? mblk[mb->s1->id] : 0,
			          mb->s2 ? mblk[mb->s2->id] : 0, MCC_NE);
			break;
		}
		case MOP_RET: {
			MInsM *t = &b->term;
			t->op = MMOP_RET;
			t->dtype = mf->rettype;
			t->src[0] = mval_of_ref(mf, mb->term.src[0]);
			if (fm->retty)
				t->td = fm->retty;
			break;
		}
		default:
			break;
		}
	}

	/* SSA phis: insert a copy of each incoming value at the end of the
	 * predecessor block, so the phi's result slot holds the merged value
	 * (all-virtual-on-stack: a plain mov works, phi arguments are values
	 * already live in their slots). */
	for (uint32_t bi = 0; bi < nblk; bi++) {
		MBlk *mb = order[bi];
		for (MPhi *p = mb->phi; p; p = p->link) {
			if (!p->dst)
				continue;
			for (uint32_t i = 0; i < p->narg; i++) {
				if (!p->blk[i] || !p->arg[i])
					continue;
				MBlkM *predm = mblk[p->blk[i]->id];
				MInsM *mi = maddm(fm, predm, MMOP_MOV, p->dtype, p->dst,
				                  p->arg[i], 0);
				/* phi-edge copy: the regalloc spills the destination so
				 * parallel-edge moves never clobber each other (no
				 * Oswap in the machine layer; a slot is safe). */
				mi->extra = 1;
			}
		}
	}

	/* If-convert simple branch diamonds to cmov (before the ABI lowering,
	 * which consumes PARM/ARG and rewrites the entry block). */
	mfnm_ifconv(fm);

	/* ABI-lower, then register-allocate and emit real x86-64 assembly
	 * (P3b/P4).  dump when MCC_DEBUG_MBE is set. */
	mfnm_abi_x86_64(fm);
	mfnm_regalloc(fm);
	if (getenv("MCC_DEBUG_MBE")) {
		fprintf(stderr, "\n> MIR backend (x86_64, post-ABI) %s:\n",
		        fm->name ? fm->name : "?");
		mfnm_dump(fm, stderr);
	}
	mfnm_emit_x86_64(fm, stdout);

	mfnm_free(fm);
	free(mblk);
	free(order);
	return true;
}
