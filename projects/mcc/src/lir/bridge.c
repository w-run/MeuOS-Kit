/* bridge.c — MIR → LIR translation layer (MIR/LIR split transition).
 *
 * Converts a fully-built MFn (MIR) into the existing QBE-derived Fn (the
 * LIR layer), so the existing opt + target backends run unchanged.  This
 * is the *only* cross-layer translation point during the transition:
 * the C frontend produces MIR (B.4), MIR passes optimize it (B.2), then
 * this bridge lowers it to Fn for instruction selection / regalloc / emit.
 *
 * The Fn construction mirrors src/irgen/emit.c:emitfunc()'s pattern
 * (forward writes into the shared insb, Opar at entry, Phi nodes, jump
 * translation) but reads from MFn instead of the frontend struct func.
 *
 * Call lowering follows the emitfunc convention:
 *   MOP_ARG sequence -> Oarg* (+ Oargc for aggregates, Oargv for varargs)
 *   then MOP_CALL terminator -> Ocall.
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "mir.h"
#include "ir.h"

/* Translate a MIR scalar type to an IR class (Kw/Kl/Ks/Kd). */
static int
mir_to_cls(MType t)
{
	switch (t) {
	case MT_I8:
	case MT_I16:
	case MT_I32:
	case MT_PTR: return Kw;
	case MT_I64: return Kl;
	case MT_F32: return Ks;
	case MT_F64: return Kd;
	default:     return Kx;
	}
}

static int
mir_to_op(MOP op)
{
	switch (op) {
	case MOP_ADD:  return Oadd;
	case MOP_SUB:  return Osub;
	case MOP_MUL:  return Omul;
	case MOP_DIV:  return Odiv;
	case MOP_UDIV: return Oudiv;
	case MOP_REM:  return Orem;
	case MOP_UREM: return Ourem;
	case MOP_NEG:  return Oneg;
	case MOP_AND:  return Oand;
	case MOP_OR:   return Oor;
	case MOP_XOR:  return Oxor;
	case MOP_SHL:  return Oshl;
	case MOP_SHR:  return Oshr;
	case MOP_SAR:  return Osar;

	case MOP_CEQ:  return Oceqw;
	case MOP_CNE:  return Ocnew;
	case MOP_CSLT: return Ocsltw;
	case MOP_CSLE: return Ocslew;
	case MOP_CSGT: return Ocsgtw;
	case MOP_CSGE: return Ocsgew;
	case MOP_CULT: return Ocultw;
	case MOP_CULE: return Oculew;
	case MOP_CUGT: return Ocugtw;
	case MOP_CUGE: return Ocugew;

	case MOP_CFEQ: return Oceqs;
	case MOP_CFNE: return Ocnes;
	case MOP_CFLT: return Oclts;
	case MOP_CFLE: return Ocles;
	case MOP_CFGT: return Ocgts;
	case MOP_CFGE: return Ocges;

	case MOP_SEXT:  return Oextsb;
	case MOP_ZEXT:  return Oextub;
	case MOP_TRUNC: return Oextuw;
	case MOP_CAST:  return Ocast;
	case MOP_F2I:   return Ostosi;
	case MOP_I2F:   return Oswtof;
	case MOP_FEXT:  return Oexts;
	case MOP_FTRUNC:return Otruncd;

	case MOP_LOAD:   return Oload;
	case MOP_STORE:  return Ostorel;
	case MOP_ALLOCA: return Oalloc16;

	case MOP_COPY:    return Ocopy;
	case MOP_VASTART: return Ovastart;
	case MOP_VAARG:   return Ovaarg;
	case MOP_SALLOC:  return Osalloc;

	default: return Onop;
	}
}

/* Map a MIR value to an IR Ref. */
static Ref
valref(MFn *fn, MVal *v, Fn *lir)
{
	if (!v)
		return R;
	switch (v->kind) {
	case MV_TEMP:
		assert(v->lirtmp >= 0 && "bridge: temp not pre-allocated");
		return TMP(v->lirtmp);
	case MV_GLOBAL: {
		Con c = {.type = CAddr};
		c.sym.id = intern(v->sym ? v->sym : "");
		c.sym.type = v->hint ? SExt : SGlo;
		if (v->tls)
			c.sym.type |= SThr;
		return newcon(&c, lir);
	}
	case MV_CONST:
		if (v->con)
			return getcon(v->con->u.i, lir);
		return CON_Z;
	case MV_TYPE:
		/* id carries the frontend/LIR typ[] index (set by func_to_mir),
		 * which TYPE() indexes directly into fn->typ. */
		return TYPE(v->id);
	default:
		return R;
	}
}

/* Translate an MRef (val or const) to an IR Ref. */
static Ref
refval(MFn *fn, MRef r, Fn *lir)
{
	if (r.con)
		switch (r.con->kind) {
		case MC_INT:
			return getcon(r.con->u.i, lir);
		case MC_FLT: {
			Con c = {.type = CBits};
			if (r.con->type == MT_F32) {
				c.bits.s = r.con->u.s;
				c.flt = 1;
			} else {
				c.bits.d = r.con->u.d;
				c.flt = 2;
			}
			return newcon(&c, lir);
		}
		case MC_ADDR: {
			Con c = {.type = CAddr};
			c.sym.id = intern(r.con->u.addr.sym ? r.con->u.addr.sym : "");
			c.sym.type = r.con->u.addr.isext ? SExt : SGlo;
			if (r.con->u.addr.tls)
				c.sym.type |= SThr;
			c.bits.i = r.con->u.addr.off;
			return newcon(&c, lir);
		}
		default:
			return CON_Z;
		}
	if (r.val)
		return valref(fn, r.val, lir);
	return R;
}

/* Build a forward-ordered array of blocks (mfn->link is reversed).
 * The array is PHeap (emalloc) so callers may free() it; the Fn and its
 * per-block Blk objects stay in the PFn pool and are reclaimed by
 * freeall() (do NOT free() those). */
static MBlk **
blk_order(MFn *mfn, uint32_t *n)
{
	uint32_t cnt = mfn->nblk;
	MBlk **o = emalloc(cnt * sizeof *o);
	uint32_t i = cnt;
	for (MBlk *b = mfn->link; b; b = b->link)
		o[--i] = b;
	*n = cnt;
	return o;
}

/* Main entry: translate a whole MFn to a LIR Fn. */
Fn *
lir_bridge(MFn *mfn)
{
	Fn *fn;
	MBlk **order;
	Blk **lirblk;
	uint32_t nblk;

	fn = alloc(sizeof *fn);
	fn->ntmp = 0;
	fn->ncon = 2;
	fn->nmem = 0;
	fn->tmp = vnew(fn->ntmp, sizeof fn->tmp[0], PFn);
	fn->con = vnew(fn->ncon, sizeof fn->con[0], PFn);
	fn->mem = vnew(0, sizeof fn->mem[0], PFn);
	for (int i = 0; i < Tmp0; i++) {
		if (T.fpr0 <= i && i < T.fpr0 + T.nfpr)
			newtmp(0, Kd, fn);
		else
			newtmp(0, Kl, fn);
	}
	fn->con[0].type = CBits;
	fn->con[0].bits.i = 0xdeaddead;
	fn->con[1].type = CBits;
	fn->con[1].bits.i = 0;

	fn->name = mfn->name ? (char *)mfn->name : "";
	fn->lnk = (Lnk){0};
	fn->lnk.export = mfn->export;
	fn->leaf = 1;
	fn->vararg = mfn->vararg;
	fn->optlevel = mfn->optlevel;
	fn->warnlevel = WARN_ALL;
	fn->slot = 0;
	fn->salign = 0;
	fn->dynalloc = 0;
	fn->retr = R;

	if (mfn->rettype == MT_VOID)
		fn->retty = -1;
	else if (mfn->retty >= 0)
		fn->retty = mfn->retty;
	else
		fn->retty = -1;

	/* pre-allocate MVal temps; record the LIR temp index per value */
	for (uint32_t i = 0; i < mfn->nval; i++) {
		MVal *v = mfn->val[i];
		if (v->kind == MV_TEMP) {
			Ref tr = newtmp(v->name ? (char *)v->name : 0,
			                mir_to_cls(v->type), fn);
			v->lirtmp = tr.val;
		}
	}

	/* pass 1: create Blk per MBlk, store mapping */
	order = blk_order(mfn, &nblk);
	lirblk = emalloc(nblk * sizeof *lirblk);
	{
		Blk *prev = NULL;
		for (uint32_t i = 0; i < nblk; i++) {
			MBlk *mb = order[i];
			Blk *qb = newblk();
			qb->id = i;
			qb->name = strf(PFn, "%s", mb->name ? mb->name : "");
			qb->loop = 0;
			qb->link = NULL;
			lirblk[i] = qb;
			if (prev)
				prev->link = qb;
			prev = qb;
		}
		fn->nblk = nblk;
		fn->start = lirblk[0];
		fn->rpo = vnew(nblk, sizeof fn->rpo[0], PFn);
	}

	/* pass 2: translate instructions, phis, jumps */
	curi = insb;
	for (uint32_t i = 0; i < nblk; i++) {
		MBlk *mb = order[i];
		Blk *qb = lirblk[i];

		/* entry block: emit Opar for parameters */
		if (mb == mfn->start) {
			for (uint32_t k = 0; k < mfn->nparam; k++) {
				MVal *p = mfn->param[k];
				if (!p)
					continue;
				if (mfn->paramty && mfn->paramty[k] >= 0) {
					/* aggregate parameter: Oparc lowers to a stack
					 * copy the callee addresses by value.  arg[0]
					 * carries TYPE(idx) (read by argsclass); the
					 * param temp (a Kl pointer) is the stack pad. */
					*curi++ = (Ins){.op = Oparc, .cls = Kl,
					                .to = valref(mfn, p, fn),
					                .arg = {TYPE(mfn->paramty[k]), R}};
				} else {
					*curi++ = (Ins){.op = Opar, .cls = mir_to_cls(p->type),
					                .to = valref(mfn, p, fn),
					                .arg = {R, R}};
				}
			}
		}

		/* phis */
		for (MPhi *p = mb->phi; p; p = p->link) {
			Phi *phi = alloc(sizeof *phi);
			phi->to = valref(mfn, p->dst, fn);
			phi->cls = mir_to_cls(p->dtype);
			phi->visit = 0;
			phi->narg = p->narg;
			phi->arg = vnew(p->narg ? p->narg : 1, sizeof phi->arg[0], PFn);
			phi->blk = vnew(p->narg ? p->narg : 1, sizeof phi->blk[0], PFn);
			for (uint32_t k = 0; k < p->narg; k++) {
				phi->arg[k] = p->arg[k] ? valref(mfn, p->arg[k], fn) : CON_Z;
				phi->blk[k] = p->blk[k] ? lirblk[p->blk[k]->id] : qb;
			}
			phi->link = NULL;
			qb->phi = phi;
		}

		/* instructions (calls lowered here; terminator handles control) */
		for (uint32_t k = 0; k < mb->nins; k++) {
			MIns *in = &mb->ins[k];
			if (in->op == MOP_PAR)
				continue; /* entry block Opar emitted above */
			if (in->op == MOP_CALL) {
				/* Oarg* sequence then Ocall.  In MIR, the MOP_ARG
				 * instructions that belong to this call appear before
				 * it in the block.  The terminator of the block holds
				 * the actual control-flow exit. */
				Ref callee = refval(mfn, in->src[0], fn);
				Ref result = in->dst ? valref(mfn, in->dst, fn) : R;
				if (in->src[1].val &&
				    in->src[1].val->kind == MV_TYPE) {
					/* aggregate return: Ocall carries TYPE(idx);
					 * selcall classifies it and lowers to SysV
					 * (RAX:RDX for ≤16 bytes, hidden sret pointer
					 * otherwise).  The result is a Kl pointer to
					 * the return pad. */
					*curi++ = (Ins){.op = Ocall, .cls = Kl,
					                .to = result,
					                .arg = {callee,
					                        refval(mfn, in->src[1], fn)}};
				} else {
					int call_cls = mir_to_cls(in->dtype);
					*curi++ = (Ins){.op = Ocall, .cls = call_cls,
					                .to = result,
					                .arg = {callee, R}};
				}
				fn->leaf = 0;
				continue;
			}
			if (in->op == MOP_ARG) {
				if (in->src[0].val &&
				    in->src[0].val->kind == MV_TYPE) {
					/* aggregate argument: Oargc copies from the
					 * source pointer; selpar lowers to stack copy
					 * or argument registers per SysV. */
					*curi++ = (Ins){.op = Oargc, .cls = Kl,
					                .to = R,
					                .arg = {refval(mfn, in->src[0], fn),
					                        refval(mfn, in->src[1], fn)}};
				} else {
					*curi++ = (Ins){.op = Oarg, .cls = mir_to_cls(in->dtype),
					                .to = R,
					                .arg = {refval(mfn, in->src[0], fn), R}};
				}
				continue;
			}
			int qop = mir_to_op(in->op);
			int cls = mir_to_cls(in->dtype);
			/* float<->int conversions: the LIR opcode encodes the source
			 * precision (Ostosi = f32->int, Odtosi = f64->int, etc.), but
			 * MIR's MOP_F2I/MOP_I2F carry the precision in the source
			 * operand's type.  Select the correct LIR opcode here. */
			if (in->op == MOP_F2I) {
				bool is64 = in->src[0].val ?
					(in->src[0].val->type == MT_F64) :
					(in->src[0].con && in->src[0].con->type == MT_F64);
				qop = is64 ? Odtosi : Ostosi;
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				*curi++ = (Ins){.op = qop, .cls = cls, .to = to,
				                .arg = {a0, R}};
				continue;
			}
			if (in->op == MOP_I2F) {
				/* source width selects the LIR op; dst width the class */
				bool src64 = in->src[0].val &&
					in->src[0].val->type == MT_I64;
				bool is64 = in->dtype == MT_F64;
				qop = src64 ? (is64 ? Osltof : Oultof)
				             : (is64 ? Oswtof : Ouwtof);
				cls = is64 ? Kd : Ks;
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				*curi++ = (Ins){.op = qop, .cls = cls, .to = to,
				                .arg = {a0, R}};
				continue;
			}
			/* sign/zero extensions: the MIR dst dtype sets the result class,
			 * but the LIR opcode must be selected from the *source* operand's
			 * width.  The opcode table maps both MOP_SEXT/MOP_ZEXT to a fixed
			 * byte-extend, so sext(i64) of an i32 constant like 256 became
			 * `extsb 256` — byte-extended to 0, silently dropping the value
			 * (a ternary with a 64-bit result broke this way). */
			if (in->op == MOP_SEXT || in->op == MOP_ZEXT) {
				MType st = in->src[0].val ? in->src[0].val->type :
				           (in->src[0].con ? in->src[0].con->type : MT_I32);
				bool s64 = in->dtype == MT_I64;
				int eop = in->op == MOP_SEXT
					? (st == MT_I8 ? Oextsb :
					   st == MT_I16 ? Oextsh : Oextsw)
					: (st == MT_I8 ? Oextub :
					   st == MT_I16 ? Oextuh : Oextuw);
				int ecls = s64 ? Kl : Kw;
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				*curi++ = (Ins){.op = eop, .cls = ecls, .to = to,
				                .arg = {a0, R}};
				continue;
			}
			/* alloca: the frontend builds align pre-padding chains
			 * (`add size, align-16`) that can fold to a negative constant
			 * (the object's align is 0 in statement-expression temporaries
			 * until the frontend fixes it).  LIR's salloc() rejects
			 * constant negative sizes; clamp to 0 so the allocation is a
			 * (correctly-aligned, possibly 0-byte) no-op like the direct
			 * path's runtime computation. */
			if (in->op == MOP_ALLOCA) {
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				if (rtype(a0) == RCon && fn->con[a0.val].bits.i <= 0)
					a0 = getcon(0, fn);
				/* The alloc result class follows the pointer width the
				 * frontend assigned to the MIR alloca destination (i32 on
				 * ILP32 targets like i386, i64 on LP64).  Hardcoding Kl
				 * makes i386 treat the result as a 64-bit stack-resident
				 * temp; seladdr then reads the slot as an address instead
				 * of folding the static allocation to [S0], corrupting
				 * every store through it. */
				int acls = in->dst ? mir_to_cls(in->dst->type) : Kl;
				*curi++ = (Ins){.op = Oalloc16, .cls = acls, .to = to,
				                .arg = {a0, R}};
				continue;
			}
			/* comparisons: the MIR dtype (fixed from the frontend opcode
			 * suffix in func_to_mir) selects the LIR width (Ocnew for i32
			 * vs Ocnel for i64, Oceqs for f32 vs Oceqd for f64). */
			if (in->op >= MOP_CEQ && in->op <= MOP_CFGE) {
				int isf = in->op >= MOP_CFEQ;
				int is64 = !isf && in->dtype == MT_I64;
				int isd = isf && in->dtype == MT_F64;
				switch (in->op) {
				case MOP_CEQ: qop = is64 ? Oceql : (isf ? (isd ? Oceqd : Oceqs) : Oceqw); break;
				case MOP_CNE: qop = is64 ? Ocnel : (isf ? (isd ? Ocned : Ocnes) : Ocnew); break;
				case MOP_CSLT: qop = is64 ? Ocsltl : (isf ? (isd ? Ocltd : Oclts) : Ocsltw); break;
				case MOP_CSLE: qop = is64 ? Ocslel : (isf ? (isd ? Ocled : Ocles) : Ocslew); break;
				case MOP_CSGT: qop = is64 ? Ocsgtl : (isf ? (isd ? Ocgtd : Ocgts) : Ocsgtw); break;
				case MOP_CSGE: qop = is64 ? Ocsgel : (isf ? (isd ? Ocged : Ocges) : Ocsgew); break;
				case MOP_CULT: qop = is64 ? Ocultl : Ocultw; break;
				case MOP_CULE: qop = is64 ? Oculel : Oculew; break;
				case MOP_CUGT: qop = is64 ? Ocugtl : Ocugtw; break;
				case MOP_CUGE: qop = is64 ? Ocugel : Ocugew; break;
				case MOP_CFEQ: qop = isd ? Oceqd : Oceqs; break;
				case MOP_CFNE: qop = isd ? Ocned : Ocnes; break;
				case MOP_CFLT: qop = isd ? Ocltd : Oclts; break;
				case MOP_CFLE: qop = isd ? Ocled : Ocles; break;
				case MOP_CFGT: qop = isd ? Ocgtd : Ocgts; break;
				case MOP_CFGE: qop = isd ? Ocged : Ocges; break;
				default: break;
				}
				/* comparisons always yield an int 0/1; the result class
				 * must be Kw (or Kl), never Ks/Kd — otherwise ssa.c:96
				 * retypes the result temp as float and downstream ext
				 * instructions trip copy.c:354's KBASE assert. */
				cls = is64 ? Kl : Kw;
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				Ref a1 = refval(mfn, in->src[1], fn);
				*curi++ = (Ins){.op = qop, .cls = cls, .to = to,
				                .arg = {a0, a1}};
				continue;
			}

			if (in->op == MOP_LOAD) {
				/* the MIR dtype is the *data* width; the LIR opcode
				 * must be selected from it (byte/halfword loads use
				 * the sign/zero-extending forms).  Previously every
				 * load mapped to Oload with cls from the data width,
				 * so `a[0] != '-'` (a char load) read 4 bytes and
				 * only options whose first word happened to equal 0x2d
				 * were recognized.  MIR loads are width-only: signed
				 * byte/halfword loads are lowered by func_to_mir to
				 * load + explicit SEXT, so here we use the
				 * zero-extending opcodes. */
				Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
				Ref a0 = refval(mfn, in->src[0], fn);
				int lop = Oload;
				/* result width from the destination's MIR type */
				int lcls = (in->dst && in->dst->type != MT_NONE)
					? mir_to_cls(in->dst->type)
					: (in->dtype == MT_I64 ? Kl :
					   in->dtype == MT_F32 ? Ks :
					   in->dtype == MT_F64 ? Kd : Kw);
				switch (in->dtype) {
				case MT_I8:  lop = Oloadub; break;
				case MT_I16: lop = Oloaduh; break;
				default:     break;  /* i32/i64/f32/f64 use Oload + cls */
				}
				*curi++ = (Ins){.op = lop, .cls = lcls, .to = to,
				                .arg = {a0, R}};
				continue;
			}
			if (in->op == MOP_STORE) {
				Ref a0 = refval(mfn, in->src[0], fn);
				Ref a1 = refval(mfn, in->src[1], fn);
				int sop = Ostorel;
				switch (in->dtype) {
				case MT_I8:  sop = Ostoreb; break;
				case MT_I16: sop = Ostoreh; break;
				case MT_I32: sop = Ostorew; break;
				case MT_I64: sop = Ostorel; break;
				case MT_F32: sop = Ostores; break;
				case MT_F64: sop = Ostored; break;
				default:     sop = Ostorel; break;
				}
				int scls = in->dtype == MT_F32 ? Ks :
				           in->dtype == MT_F64 ? Kd : Kw;
				*curi++ = (Ins){.op = sop, .cls = scls, .to = R,
				                .arg = {a0, a1}};
				continue;
			}

			Ref to = in->dst ? valref(mfn, in->dst, fn) : R;
			Ref a0 = refval(mfn, in->src[0], fn);
			Ref a1 = refval(mfn, in->src[1], fn);
			*curi++ = (Ins){.op = qop, .cls = cls, .to = to, .arg = {a0, a1}};
		}

		idup(qb, insb, curi - insb);
		curi = insb;

		/* terminator */
		switch (mb->term.op) {
		case MOP_JMP:
			qb->jmp.type = Jjmp;
			qb->s1 = lirblk[mb->s1->id];
			break;
		case MOP_JNZ:
			qb->jmp.type = Jjnz;
			qb->jmp.arg = refval(mfn, mb->term.src[0], fn);
			qb->s1 = lirblk[mb->s1->id];
			qb->s2 = lirblk[mb->s2->id];
			break;
		case MOP_RET:
			if (mb->term.src[0].val || mb->term.src[0].con) {
				if (mfn->retty >= 0) {
					/* aggregate return: Jretc uses the typ[] entry at
					 * fn->retty; selret lowers the copy-out (RAX:RDX
					 * pack or hidden sret write). */
					qb->jmp.type = Jretc;
				} else {
					int rty = mir_to_cls(mfn->rettype);
					qb->jmp.type = Jretw + (rty >= 0 ? rty : 0);
				}
				qb->jmp.arg = refval(mfn, mb->term.src[0], fn);
			} else {
				qb->jmp.type = Jret0;
			}
			break;
		default:
			qb->jmp.type = Jret0;
			break;
		}
	}

	free(order);
	free(lirblk);
	return fn;
}
