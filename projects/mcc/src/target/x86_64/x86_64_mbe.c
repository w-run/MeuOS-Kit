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
map_op(MOP op)
{
	switch (op) {
	case MOP_ADD:  return MMOP_ADD;
	case MOP_SUB:  return MMOP_SUB;
	case MOP_MUL:  return MMOP_MUL;
	case MOP_DIV:  return MMOP_DIV;
	case MOP_UDIV: return MMOP_UDIV;
	case MOP_REM:  return MMOP_REM;
	case MOP_UREM: return MMOP_UREM;
	case MOP_AND:  return MMOP_AND;
	case MOP_OR:   return MMOP_OR;
	case MOP_XOR:  return MMOP_XOR;
	case MOP_SHL:  return MMOP_SHL;
	case MOP_SHR:  return MMOP_SHR;
	case MOP_SAR:  return MMOP_SAR;
	case MOP_NEG:  return MMOP_NEG;
	case MOP_COPY: return MMOP_MOV;
	case MOP_CAST: return MMOP_MOV;
	case MOP_SEXT: return MMOP_MOVSX;
	case MOP_ZEXT: return MMOP_MOVZX;
	case MOP_ALLOCA: return MMOP_ALLOCA16;
	case MOP_SALLOC: return MMOP_SALLOC;
	case MOP_VASTART: return MMOP_VASTART;
	case MOP_VAARG:   return MMOP_VAARG;
	default:       return MMOP_NONE;
	}
}

static MVal *
mref_val(MRef r)
{
	return r.val ? r.val : 0;
}

void
mfnm_backend_x86_64(MFn *mf)
{
	/* Aggregate parameter/return types are carried in func_to_mir only as
	 * frontend type ids (MV_TYPE.id = LIR typ[] index); the MIR value
	 * table has no MTypeDesc for them yet (func_to_mir does not fill
	 * mf->typ).  The machine ABI needs MField layouts, so aggregate
	 * functions are skipped here until P3 wires MTypeDesc into the MIR
	 * type system.  Scalar functions run the full convert + ABI path. */
	bool agg = mf->rettype == MT_AGG;
	if (!agg)
		for (uint32_t j = 0; j < mf->nparam && !agg; j++)
			if (mf->paramty && mf->paramty[j] >= 0)
				agg = true;
	if (!agg)
		for (MBlk *mb = mf->link; mb && !agg; mb = mb->link)
			for (uint32_t k = 0; k < mb->nins && !agg; k++) {
				MIns *in = &mb->ins[k];
				if ((in->op == MOP_ARG && in->src[0].val &&
				     in->src[0].val->kind == MV_TYPE) ||
				    (in->op == MOP_CALL && in->src[1].val &&
				     in->src[1].val->kind == MV_TYPE))
					agg = true;
			}
	if (agg) {
		fprintf(stderr, "[mbe] skip %s (aggregate; MIR type info is P3)\n",
		        mf->name);
		return;
	}
	MFnM *fm = mfnm_new(mf, &mtarget_x86_64, mf->name);
	fm->retty = 0;

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
				/* locate the aggregate marker: paramty[k] */
				int pty = -1;
				for (uint32_t j = 0; j < mf->nparam; j++)
					if (mf->param[j] == dst) {
						pty = mf->paramty ? mf->paramty[j] : -1;
						break;
					}
				MInsM *mi = maddm(fm, b, MMOP_PARM, dst ? dst->type : MT_NONE,
				                  dst, 0, 0);
				if (pty >= 0)
					mi->td = mf->typ[pty];
				break;
			}
			case MOP_ARG: {
				MInsM *mi = maddm(fm, b, MMOP_ARG, in->dtype,
				                  mref_val(in->src[0]), 0, 0);
				if (in->src[0].val && in->src[0].val->kind == MV_TYPE) {
					/* aggregate: src[1] is the source pointer */
					mi->td = mf->typ[in->src[0].val->id];
					mi->src[0] = mref_val(in->src[1]);
				}
				break;
			}
			case MOP_CALL: {
				MInsM *mi = maddm(fm, b, MMOP_CALL, in->dtype, dst,
				                  mref_val(in->src[0]), 0);
				if (in->src[1].val && in->src[1].val->kind == MV_TYPE)
					mi->td = mf->typ[in->src[1].val->id];
				break;
			}
			case MOP_LOAD: {
				maddm_addr(fm, b, MMOP_LOAD, in->dtype, dst,
				           maddr(mref_val(in->src[0]), 0, 1, 0), 0);
				break;
			}
			case MOP_STORE: {
				maddm_addr(fm, b, MMOP_STORE, in->dtype, 0,
				           maddr(mref_val(in->src[0]), 0, 1, 0),
				           mref_val(in->src[1]));
				break;
			}
			case MOP_VASTART:
				maddm(fm, b, MMOP_VASTART, in->dtype, 0,
				      mref_val(in->src[0]), 0);
				break;
			case MOP_VAARG:
				maddm(fm, b, MMOP_VAARG, in->dtype, dst,
				      mref_val(in->src[0]), 0);
				break;
			default: {
				MMOP mo = map_op(in->op);
				if (mo == MMOP_NONE)
					break;
				MInsM *mi = maddm(fm, b, mo, in->dtype, dst,
				                   mref_val(in->src[0]), mref_val(in->src[1]));
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
			MVal *cond = mref_val(mb->term.src[0]);
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
			t->src[0] = mref_val(mb->term.src[0]);
			if (fm->retty)
				t->td = fm->retty;
			break;
		}
		default:
			break;
		}
	}

	/* ABI-lower and dump */
	mfnm_abi_x86_64(fm);
	fprintf(stderr, "\n> MIR backend (x86_64, post-ABI) %s:\n",
	        fm->name ? fm->name : "?");
	mfnm_dump(fm, stderr);

	mfnm_free(fm);
	free(mblk);
	free(order);
}
