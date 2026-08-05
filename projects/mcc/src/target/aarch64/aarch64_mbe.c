/* aarch64_mbe.c — aarch64 machine-backend entry (MIR-native).
 *
 * Translates MIR functions into the shared machine IR (MMOP) and runs the
 * aarch64 ABI lowering + register allocation + assembly emission.
 *
 * Comparison model: aarch64 has NZCV condition flags (cmp sets them,
 * b.cc branches on them, cset materializes a 0/1).  The isel still
 * lowers MIR comparisons to MMOP_SETCCR ("dst = a cc b ? 1 : 0") and
 * `if (bool)` to MMOP_JCC with the boolean in src[0] — the emit layer
 * implements SETCCR with `cmp a, b; cset dst, cc` and JCC with cbnz/cbz.
 * This mirrors the riscv64 register-comparison scheme and avoids the
 * flags-liveness problem (a compare and its branch can never be separated
 * by a flag-clobbering instruction), at a small cost of one extra cset.
 *
 * mbe_supported() restricts the MIR-native path to scalar integer
 * functions (no aggregates, floats, varargs, TLS, VLA) for this round;
 * everything else falls back to the legacy LIR aarch64 backend.
 */
#include <stdlib.h>

#include "mir.h"
#include "aarch64_m.h"

extern void mfnm_abi_aarch64(MFnM *fm);
extern void mfnm_emit_aarch64(MFnM *fm, FILE *f);

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
	case MOP_ALLOCA: return MMOP_ALLOCA16;
	default:       return MMOP_NONE;
	}
}

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
	case MOP_CFLT: return MCC_LT;   /* FP compare: signed-style names */
	case MOP_CFLE: return MCC_LE;
	case MOP_CFGT: return MCC_GT;
	case MOP_CFGE: return MCC_GE;
	default:       return MCC_EQ;
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
mval_of_ref(MFn *mf, MRef r)
{
	if (r.val)
		return r.val;
	if (r.con)
		return mval_const(mf, r.con->type, r.con);
	return 0;
}

/* Full coverage: scalar, float, aggregates, VLA and varargs all run
 * MIR-native on aarch64. */
static bool
mbe_supported(MFn *mf)
{
	(void)mf;
	return true;
}

bool
mfnm_backend_aarch64(MFn *mf)
{
	if (!mbe_supported(mf))
		return false;
	MFnM *fm = mfnm_new(mf, &mtarget_aarch64, mf->name);
	fm->retty = mf->rettyd;

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
				MInsM *mi = maddm(fm, b, MMOP_PARM, dst ? dst->type : MT_NONE,
				                  dst, 0, 0);
				mi->td = dst ? dst->td : 0;
				break;
			}
			case MOP_ARG:
				maddm(fm, b, MMOP_ARG, in->dtype, 0,
				      mval_of_ref(mf, in->src[0]), 0);
				break;
			case MOP_CALL: {
				if (dst && dst->type == MT_NONE)
					dst->type = in->dtype;
				MInsM *mi = maddm(fm, b, MMOP_CALL, in->dtype, dst,
				                  mval_of_ref(mf, in->src[0]), 0);
				(void)mi;
				break;
			}
			case MOP_LOAD: {
				maddm_addr(fm, b, MMOP_LOAD, in->dtype, dst,
				           maddr(mval_of_ref(mf, in->src[0]), 0, 1, 0), 0);
				break;
			}
			case MOP_STORE: {
				/* MIR convention: src[0] = value, src[1] = address */
				maddm_addr(fm, b, MMOP_STORE, in->dtype, 0,
				           maddr(mval_of_ref(mf, in->src[1]), 0, 1, 0),
				           mval_of_ref(mf, in->src[0]));
				break;
			}
			default: {
				if (in->op >= MOP_CEQ && in->op <= MOP_CFGE) {
					/* comparison -> SETCCR (cmp + cset at emit) */
					MVal *a0 = mval_of_ref(mf, in->src[0]);
					MVal *a1 = mval_of_ref(mf, in->src[1]);
					if (a0 && a1) {
						MInsM *ci = maddm(fm, b, MMOP_SETCCR, in->dtype,
						                  dst, a0, a1);
						ci->cc = mir_cmp_cc(in->op);
					}
					break;
				}
				/* int<->fp conversions and fp widening/narrowing */
				if (in->op == MOP_F2I || in->op == MOP_I2F ||
				    in->op == MOP_UI2F || in->op == MOP_FEXT ||
				    in->op == MOP_FTRUNC) {
					MVal *a0 = mval_of_ref(mf, in->src[0]);
					MMOP co;
					switch (in->op) {
					case MOP_F2I:
						co = (a0 && a0->type == MT_F64)
						         ? MMOP_CVTTSD2SI : MMOP_CVTTSS2SI;
						break;
					case MOP_I2F:
						co = in->dtype == MT_F64 ? MMOP_CVTSI2SD
						                         : MMOP_CVTSI2SS;
						break;
					case MOP_UI2F:
						co = in->dtype == MT_F64 ? MMOP_CVTSI2SD_U
						                         : MMOP_CVTSI2SS_U;
						break;
					case MOP_FEXT:   co = MMOP_CVTSS2SD; break;
					default:         co = MMOP_CVTSD2SS; break;
					}
					maddm(fm, b, co, in->dtype, dst, a0, 0);
					break;
				}
				MMOP mo = map_op(in->op,
				                in->dtype == MT_F32 || in->dtype == MT_F64);
				if (mo == MMOP_NONE)
					break;
				MInsM *mi = maddm(fm, b, mo, in->dtype, dst,
				                   mval_of_ref(mf, in->src[0]),
				                   mval_of_ref(mf, in->src[1]));
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
			/* branch on the boolean value: JCC carries it in src[0]
			 * (cbnz/cbz at emit), no flags involved */
			MVal *cond = mval_of_ref(mf, mb->term.src[0]);
			MBlkM *t = mb->s1 ? mblk[mb->s1->id] : 0;
			MBlkM *ft = mb->s2 ? mblk[mb->s2->id] : 0;
			mfnm_term(fm, b, MMOP_JCC, cond, t, ft, MCC_NE);
			b->term.src[1] = mval_const(mf, MT_I64,
			                            mconst_int(mf, MT_I64, 0));
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

	/* SSA phis: pred-edge copies (same as x86_64_mbe.c) */
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
				mi->extra = 1;
			}
		}
	}

	mfnm_abi_aarch64(fm);
	mfnm_regalloc(fm);
	if (getenv("MCC_DEBUG_MBE")) {
		fprintf(stderr, "\n> MIR backend (aarch64) %s:\n",
		        fm->name ? fm->name : "?");
		mfnm_dump(fm, stderr);
	}
	mfnm_emit_aarch64(fm, stdout);

	mfnm_free(fm);
	free(mblk);
	free(order);
	return true;
}
