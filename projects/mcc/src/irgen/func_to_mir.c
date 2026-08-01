/* func_to_mir.c — translate the frontend `struct func` (AST→IR builder
 * tree) into a MIR MFn.
 *
 * This is the B.4.2 transitional layer: the C frontend continues to build
 * its existing `struct func`/`struct value`/`struct inst` tree (zero
 * changes to src/{lex,parse,sema,irgen}), and emitfunc now lowers that
 * tree to MIR (MFn), runs the MIR passes, bridges to the LIR Fn, and runs
 * the LIR pipeline.  The result: C compilation genuinely flows through the
 * new MIR layer, and the MIR passes get real-workload validation.
 *
 * Mapping conventions mirror src/irgen/emit.c:emitfunc():
 *   - frontend value ids start at 1 (functemp increments f->lastid); we
 *     keep a side table value_id -> MVal*
 *   - frontend ops (enum instkind) map to MOP via fe_to_mir_op()
 *   - jumps map to MIR terminators (MOP_JMP/JNZ/RET)
 *   - calls: the frontend emits ICALL followed by IARG* (+ IVARARG);
 *     we lower to MOP_ARG* + MOP_CALL
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "irgen.h"
#include "mir.h"

/* Translate a frontend class char ('w','l','s','d', 0) to a MIR scalar
 * type.  Returns MT_NONE for 0/unknown. */
static MType
fe_cls_to_mtype(int c)
{
	switch (c) {
	case 'w': return MT_I32;
	case 'l': return MT_I64;
	case 's': return MT_F32;
	case 'd': return MT_F64;
	default:  return MT_NONE;
	}
}

/* Translate a frontend op (enum instkind) to a MIR MOP. */
static MOP
fe_to_mir_op(int op)
{
	switch (op) {
	/* arithmetic / bits */
	case IADD:    return MOP_ADD;
	case ISUB:    return MOP_SUB;
	case IMUL:    return MOP_MUL;
	case IDIV:    return MOP_DIV;
	case IUDIV:   return MOP_UDIV;
	case IREM:    return MOP_REM;
	case IUREM:   return MOP_UREM;
	case INEG:    return MOP_NEG;
	case IAND:    return MOP_AND;
	case IOR:     return MOP_OR;
	case IXOR:    return MOP_XOR;
	case ISAR:    return MOP_SAR;
	case ISHR:    return MOP_SHR;
	case ISHL:    return MOP_SHL;
	/* sign/zero extensions */
	case IEXTUB:  return MOP_ZEXT;
	case IEXTSB:  return MOP_SEXT;
	case IEXTUH:  return MOP_ZEXT;
	case IEXTSH:  return MOP_SEXT;
	case IEXTUW:  return MOP_ZEXT;
	case IEXTSW:  return MOP_SEXT;
	/* float conversions */
	case IEXTS:   return MOP_FEXT;
	case ITRUNCD: return MOP_FTRUNC;
	case ISTOSI:  return MOP_F2I;
	case ISTOUI:  return MOP_F2I;
	case IDTOSI:  return MOP_F2I;
	case IDTOUI:  return MOP_F2I;
	case ISWTOF:  return MOP_I2F;
	case IUWTOF:  return MOP_I2F;
	case ISLTOF:  return MOP_I2F;
	case IULTOF:  return MOP_I2F;
	/* cast / copy */
	case ICAST:   return MOP_CAST;
	case ICOPY:   return MOP_COPY;
	/* memory store */
	case ISTOREB:
	case ISTOREH:
	case ISTOREW:
	case ISTOREL:
	case ISTORES:
	case ISTORED: return MOP_STORE;
	/* memory load */
	case ILOADUB:
	case ILOADSB:
	case ILOADUH:
	case ILOADSH:
	case ILOADW:
	case ILOADL:
	case ILOADS:
	case ILOADD:  return MOP_LOAD;
	/* stack alloc */
	case IALLOC4:
	case IALLOC8:
	case IALLOC16: return MOP_ALLOCA;
	/* comparisons */
	case ICEQW: return MOP_CEQ;
	case ICNEW: return MOP_CNE;
	case ICSLEW:return MOP_CSLE;
	case ICSLTW:return MOP_CSLT;
	case ICSGEW:return MOP_CSGE;
	case ICSGTW:return MOP_CSGT;
	case ICULEW:return MOP_CULE;
	case ICULTW:return MOP_CULT;
	case ICUGEW:return MOP_CUGE;
	case ICUGTW:return MOP_CUGT;
	case ICEQL: return MOP_CEQ;
	case ICNEL: return MOP_CNE;
	case ICSLEL:return MOP_CSLE;
	case ICSLTL:return MOP_CSLT;
	case ICSGEL:return MOP_CSGE;
	case ICSGTL:return MOP_CSGT;
	case ICULEL:return MOP_CULE;
	case ICULTL:return MOP_CULT;
	case ICUGEL:return MOP_CUGE;
	case ICUGTL:return MOP_CUGT;
	case ICEQS: return MOP_CFEQ;
	case ICNES: return MOP_CFNE;
	case ICLES: return MOP_CFLE;
	case ICLTS: return MOP_CFLT;
	case ICGES: return MOP_CFGE;
	case ICGTS: return MOP_CFGT;
	case ICEQD: return MOP_CFEQ;
	case ICNED: return MOP_CFNE;
	case ICLED: return MOP_CFLE;
	case ICLTD: return MOP_CFLT;
	case ICGED: return MOP_CFGE;
	case ICGTD: return MOP_CFGT;
	/* variadic */
	case IVASTART: return MOP_VASTART;
	case IVAARG:   return MOP_VAARG;
	case ICALL:    return MOP_CALL;
	case IARG:     return MOP_ARG;
	case IVARARG:  return MOP_VARARG;
	default:
		die("mcc: frontend op %d not mapped to MIR", op);
	}
}

/* Map a frontend `struct value *` to a MVal, creating it on first use.
 * `tab` is indexed by v->id (frontend temp ids start at 1); only
 * VALUE_TEMP values use the tab (their ids are the temp sequence).
 * Constants/globals/types are rebuilt on each reference (cheap, and their
 * ids are not temp ids so they must not index the tab). */
static MVal *
fe_val(MFn *fn, struct value *v, MVal **tab)
{
	MVal *m;
	int cls;

	if (!v || v->kind == VALUE_NONE)
		return 0;
	cls = v->kind & 0xf;
	if (cls == VALUE_TEMP) {
		if (v->id > 0 && tab[v->id])
			return tab[v->id];
		m = mval_new(fn, MV_TEMP, MT_NONE, 0,
		             v->u.name ? v->u.name : 0);
		tab[v->id] = m;
		return m;
	}

	switch (cls) {
	case VALUE_GLOBAL: {
		/* String literals (VALUE_QUOTE) are emitted by emitdata as
		 * local `.L<name>.<id>` symbols; reference them with that
		 * local name so the LIR bridge produces a correct relocation.
		 * Other globals use the bare name (extern) or `.L<name>.<id>`
		 * (internal, id > 0).  Thread-local globals keep their symbol
		 * and the TLS flag. */
		bool tls = (v->kind & VALUE_THREAD) != 0;
		const char *nm = v->u.name ? v->u.name : "compound";
		if (v->kind & VALUE_QUOTE) {
			char buf[256];
			snprintf(buf, sizeof buf, ".L%s.%u", nm, v->id);
			m = mval_global(fn, buf, false, false);
		} else if (v->id && !(v->kind & VALUE_EXTERN) && !tls) {
			char buf[256];
			snprintf(buf, sizeof buf, ".L%s.%u", nm, v->id);
			m = mval_global(fn, buf, false, false);
		} else {
			m = mval_global(fn, v->u.name,
			                (v->kind & VALUE_EXTERN) != 0, tls);
		}
		break;
	}
	case VALUE_INTCONST: {
		MConst *c = mconst_int(fn, MT_I64, (int64_t)v->u.i);
		m = mval_const(fn, MT_I64, c);
		break;
	}
	case VALUE_FLTCONST: {
		MConst *c = mconst_flt(fn, MT_F32, v->u.f);
		m = mval_const(fn, MT_F32, c);
		break;
	}
	case VALUE_DBLCONST: {
		MConst *c = mconst_flt(fn, MT_F64, v->u.f);
		m = mval_const(fn, MT_F64, c);
		break;
	}
	case VALUE_TYPE:
		m = mval_new(fn, MV_TYPE, MT_AGG, 0,
		             v->u.name ? v->u.name : "type");
		break;
	case VALUE_LABEL:
		m = mval_new(fn, MV_LABEL, MT_NONE, 0,
		             v->u.name ? v->u.name : "label");
		break;
	default:
		m = mval_new(fn, MV_TEMP, MT_NONE, 0, "?");
		break;
	}
	return m;
}

/* Set the MIR type of a temp MVal from a frontend class char, but only if
 * the value is a temp and its MIR type is still MT_NONE. */
static void
fe_typ(MVal *m, int class)
{
	if (m && m->kind == MV_TEMP && m->type == MT_NONE) {
		MType t = fe_cls_to_mtype(class);
		if (t != MT_NONE)
			m->type = t;
	}
}

/* Resolve a frontend value reference to an MRef (value or constant). */
static MRef
fe_ref(MFn *fn, struct value *v, MVal **tab)
{
	MVal *m = fe_val(fn, v, tab);
	if (!m)
		return (MRef){0};
	if (m->kind == MV_CONST && m->con)
		return MREF_CON(m->con);
	return MREF_VAL(m);
}

/* Main entry: translate a whole frontend function into a MIR MFn. */
MFn *
func_to_mir(struct func *f, int optlevel)
{
	MFn *fn;
	MVal **tab;
	struct block **fbmap; /* frontend block order */
	struct decl *p;
	struct value *v;
	struct block *b;
	uint32_t nblk = 0, bi = 0;

	fn = mfn_new(f->name, optlevel);
	fn->rettype = MT_NONE;
	if (f->type->base && f->type->base->kind == TYPEVOID)
		fn->rettype = MT_VOID;
	else if (f->type->base && !f->type->base->value) {
		struct irtype qt = irtype(f->type->base);
		MType t = fe_cls_to_mtype(qt.base);
		if (t != MT_NONE)
			fn->rettype = t;
	}
	fn->vararg = f->type->u.func.isvararg;

	/* value side table: indexed by frontend value id (1..lastid) */
	tab = xmalloc((f->lastid + 1) * sizeof *tab);
	memset(tab, 0, (f->lastid + 1) * sizeof *tab);

	/* count blocks */
	for (b = f->start; b; b = b->next)
		nblk++;
	fbmap = xmalloc(nblk * sizeof *fbmap);

	/* create a MBlk per frontend block; record block->MBlk mapping */
	for (b = f->start, bi = 0; b; b = b->next, bi++) {
		MBlk *mb = mblk_new(fn, b->label.u.name);
		mfn_addblk(fn, mb);
		fbmap[bi] = b;
		b->ir = (Blk *)mb; /* stash MBlk* in the unused ir slot */
	}

	/* parameters */
	for (p = f->type->u.func.params, v = f->paramtemps;
	     p; p = p->next, ++v) {
		if (v->kind == VALUE_NONE)
			continue;
		MVal *mv = fe_val(fn, v, tab);
		struct irtype qt = irtype(p->type);
		MType t = fe_cls_to_mtype(qt.base);
		if (mv && t != MT_NONE)
			mv->type = t;
		fn->param = realloc(fn->param,
		                    (fn->nparam + 1) * sizeof *fn->param);
		fn->param[fn->nparam++] = mv;
	}

	/* entry block: emit MOP_PAR for parameters */
	{
		MBlk *entry = (MBlk *)f->start->ir;
		uint32_t k = 0;
		for (p = f->type->u.func.params, v = f->paramtemps;
		     p; p = p->next, ++v) {
			if (v->kind == VALUE_NONE)
				continue;
			MVal *mv = tab[v->id];
			if (mv)
				madd1(fn, entry, MOP_PAR, mv->type,
				      mv, MREF_CON(0));
			k++;
		}
	}

	/* translate blocks */
	for (b = f->start; b; b = b->next) {
		MBlk *mb = (MBlk *)b->ir;
		struct inst **instp, **instend;

		/* phi */
		if (b->phi.res.kind != VALUE_NONE) {
			MVal *d = fe_val(fn, &b->phi.res, tab);
			MType dt = fe_cls_to_mtype(b->phi.class);
			if (d && dt != MT_NONE)
				d->type = dt;
			mphi_add(fn, mb, dt, d);
			/* fill phi args from the frontend 2-arg phi */
			MPhi *p = mb->phi;
			p->narg = 2;
			p->carg = 2;
			p->arg = malloc(2 * sizeof *p->arg);
			p->blk = malloc(2 * sizeof *p->blk);
			p->arg[0] = fe_val(fn, b->phi.val[0], tab);
			p->arg[1] = fe_val(fn, b->phi.val[1], tab);
			p->blk[0] = (MBlk *)b->phi.blk[0]->ir;
			p->blk[1] = (MBlk *)b->phi.blk[1]->ir;
		}

		/* instructions */
		instend = (struct inst **)((char *)b->insts.val + b->insts.len);
		for (instp = b->insts.val; instp != instend; ++instp) {
			struct inst *in = *instp;

			if (in->kind == ICALL) {
				struct inst **argp = instp + 1;
				MVal *callee = fe_val(fn, in->arg[0], tab);
				while (argp != instend) {
					struct inst *ai = *argp;
					if (ai->kind == IARG) {
						madd1(fn, mb, MOP_ARG,
						      fe_cls_to_mtype(ai->class),
						      0,
						      fe_ref(fn, ai->arg[0], tab));
					} else if (ai->kind == IVARARG) {
						madd0(fn, mb, MOP_VARARG,
						      MT_NONE, 0);
					} else {
						break;
					}
					argp++;
				}
				MVal *res = in->res.kind ?
					fe_val(fn, &in->res, tab) : 0;
				MType rt = in->class ?
					fe_cls_to_mtype(in->class) : MT_NONE;
				madd1(fn, mb, MOP_CALL, rt, res,
				      MREF_VAL(callee));
				instp = argp - 1;
				continue;
			}

			MOP op = fe_to_mir_op(in->kind);
			MRef a0 = fe_ref(fn, in->arg[0], tab);
			MRef a1 = fe_ref(fn, in->arg[1], tab);
			MVal *res = in->res.kind ?
				fe_val(fn, &in->res, tab) : 0;
			MType dt = fe_cls_to_mtype(in->class);
			fe_typ(res, in->class);

			if (op == MOP_STORE) {
				madd(fn, mb, op, dt, 0, a0, a1);
			} else if (a1.val || a1.con) {
				madd(fn, mb, op, dt, res, a0, a1);
			} else {
				madd1(fn, mb, op, dt, res, a0);
			}
		}

		/* jump */
		switch (b->jump.kind) {
		case JUMP_RET:
			if (b->jump.arg) {
				MRef r = fe_ref(fn, b->jump.arg, tab);
				mret(fn, mb, r);
			} else {
				mretvoid(fn, mb);
			}
			break;
		case JUMP_JMP:
			mterm(fn, mb, MOP_JMP, MREF_CON(0),
			      (MBlk *)b->jump.blk[0]->ir, 0);
			break;
		case JUMP_JNZ:
			mterm(fn, mb, MOP_JNZ,
			      fe_ref(fn, b->jump.arg, tab),
			      (MBlk *)b->jump.blk[0]->ir,
			      (MBlk *)b->jump.blk[1]->ir);
			break;
		case JUMP_NONE:
			if (b->next)
				mterm(fn, mb, MOP_JMP, MREF_CON(0),
				      (MBlk *)b->next->ir, 0);
			else
				mretvoid(fn, mb);
			break;
		case JUMP_HLT:
			mretvoid(fn, mb);
			break;
		default:
			mretvoid(fn, mb);
			break;
		}
	}

	free(tab);
	free(fbmap);
	return fn;
}
