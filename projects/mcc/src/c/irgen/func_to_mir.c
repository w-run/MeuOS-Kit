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

/* MIR MIns.extra bit marking a volatile load/store (frontend INST_VOLATILE
 * propagated through).  Bit 1 deliberately avoids the machine layer's
 * extra==1 phi-spill marker; MIR-level extra is otherwise unused. */
#define MIR_VOLATILE (2u)

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

/* ---- P3a: frontend aggregate type -> MTypeDesc -------------------------
 * Builds the MIR aggregate type tree (MField with explicit offsets, arrays
 * as MTypeDesc::is_array, nested aggregates recursed) so the MIR machine
 * layer can run the SysV ABI without the LIR typ[] table.  The MV_TYPE
 * value keeps its id (frontend/LIR typ[] index) for the bridge. */

/* frontend scalar type -> MIR scalar MType (array wrappers already peeled) */
static MType
fe_scalar_mtype(struct type *t)
{
	switch (t->kind) {
	case TYPEBOOL:
	case TYPECHAR:    return MT_I8;
	case TYPESHORT:   return MT_I16;
	case TYPEINT:
	case TYPEENUM:    return MT_I32;
	case TYPELONG:
	case TYPELLONG:   return MT_I64;
	case TYPEPOINTER:
	case TYPENULLPTR: return MT_PTR;
	case TYPEFLOAT:   return MT_F32;
	case TYPEDOUBLE:  return MT_F64;
	default:          return MT_NONE;
	}
}

static MTypeDesc *
fe_to_mtd(MFn *fn, struct type *t)
{
	/* cache: same frontend type shares one MTypeDesc (also bounds the
	 * recursion for nested aggregates) */
	for (uint32_t i = 0; i < fn->ntdc; i++)
		if (fn->tdkey[i] == t)
			return fn->tdcache[i];

	MTypeDesc *td;
	if (t->kind == TYPEARRAY) {
		struct type *el = t->base;
		uint64_t esz = el->size ? el->size : 1;
		uint64_t nelem = t->size / esz;
		if (el->kind == TYPESTRUCT || el->kind == TYPEUNION ||
		    el->kind == TYPEARRAY) {
			td = mtd_array(fe_to_mtd(fn, el), nelem);
		} else {
			/* scalar element: manual desc carrying elem_type */
			td = calloc(1, sizeof *td);
			td->is_array = true;
			td->elem_type = fe_scalar_mtype(el);
			td->nelem = nelem;
			td->size = t->size;
			td->align = el->align ? el->align : 1;
		}
	} else {
		td = mtd_new(t->u.structunion.tag, t->kind == TYPEUNION);
		td->size = t->size;
		td->align = t->align ? t->align : 1;
		td->is_incomplete = t->incomplete;
		for (struct member *m = t->u.structunion.members; m; m = m->next) {
			struct type *sub = m->type;
			if (!sub || sub->kind == TYPEFUNC)
				continue;   /* C++ member functions occupy no storage */
			if (sub->kind == TYPEARRAY ||
			    sub->kind == TYPESTRUCT || sub->kind == TYPEUNION) {
				/* array or nested aggregate member */
				mtd_add_field(td, m->name, MT_AGG,
				              fe_to_mtd(fn, sub),
				              (int64_t)m->offset, -1, 0);
			} else {
				MType st = fe_scalar_mtype(sub);
				if (st == MT_NONE)
					continue;
				if (m->bits.before || m->bits.after) {
					/* bit-field: storage unit type + bit offset/width */
					int unitbits = (int)(sub->size * 8);
					int width = unitbits - m->bits.before - m->bits.after;
					mtd_add_field(td, m->name, st, 0,
					              (int64_t)m->offset,
					              m->bits.before, width);
				} else {
					mtd_add_field(td, m->name, st, 0,
					              (int64_t)m->offset, -1, 0);
				}
			}
		}
	}
	mtd_finalize(td);
	mfn_addtype(fn, td);
	fn->tdkey = realloc(fn->tdkey, (fn->ntdc + 1) * sizeof *fn->tdkey);
	fn->tdcache = realloc(fn->tdcache, (fn->ntdc + 1) * sizeof *fn->tdcache);
	fn->tdkey[fn->ntdc] = t;
	fn->tdcache[fn->ntdc] = td;
	fn->ntdc++;
	return td;
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
	case IUWTOF:  return MOP_UI2F;
	case ISLTOF:  return MOP_I2F;
	case IULTOF:  return MOP_UI2F;
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
		 * and the TLS flag.  VALUE_QUOTE is also used for asm-named
		 * symbols (`__asm__` or C++ namespace mangling); those carry
		 * id == 0 and an external linkage, and keep the bare name. */
		bool tls = (v->kind & VALUE_THREAD) != 0;
		const char *nm = v->u.name ? v->u.name : "compound";
		if ((v->kind & VALUE_QUOTE) && v->id) {
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
	case VALUE_TYPE: {
		/* the id stays the frontend/LIR typ[] index (bridge emits
		 * TYPE(idx)); td carries the MIR MTypeDesc for the machine
		 * layer (P3a). */
		struct type *ft = typeforvalue(v->id);
		m = mval_new(fn, MV_TYPE, MT_AGG,
		             ft ? fe_to_mtd(fn, ft) : 0,
		             v->u.name ? v->u.name : "type");
		m->id = v->id;
		break;
	}
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
func_to_mir(struct func *f, int optlevel, bool export)
{
	MFn *fn;
	MVal **tab;
	struct block **fbmap; /* frontend block order */
	struct decl *p;
	struct value *v;
	struct block *b;
	uint32_t nblk = 0, bi = 0;

	fn = mfn_new(f->name, optlevel);
	fn->export = export;
	fn->rettype = MT_NONE;
	if (f->type->base && f->type->base->kind == TYPEVOID)
		fn->rettype = MT_VOID;
	else if (f->type->base && f->type->base->value) {
		/* aggregate return: MT_AGG + the typ[] index.  The bridge
		 * emits Jretc and selret packs the value into RAX:RDX (or
		 * copies to the hidden sret pointer) per SysV.  rettyd carries
		 * the MIR MTypeDesc for the machine-layer backend. */
		fn->rettype = MT_AGG;
		fn->retty = f->type->base->value->id;
		fn->rettyd = fe_to_mtd(fn, f->type->base);
	} else if (f->type->base && !f->type->base->value) {
		struct irtype qt = irtype(f->type->base);
		MType t = fe_cls_to_mtype(qt.base);
		if (t != MT_NONE)
			fn->rettype = t;
	}
	fn->vararg = f->type->u.func.isvararg;

	/* value side table: indexed by frontend value id (1..lastid).
	 * Kept on the MFn so the DWARF collector can map frontend locals to
	 * their MIR values (and final stack slots) after the machine backend
	 * runs; mfn_free() releases it. */
	tab = xmalloc((f->lastid + 1) * sizeof *tab);
	memset(tab, 0, (f->lastid + 1) * sizeof *tab);
	fn->vmap = tab;
	fn->nvmap = f->lastid + 1;

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
	fn->paramty = xmalloc((f->type->u.func.nparam + 1) * sizeof *fn->paramty);
	{
		uint32_t pi = 0;
	for (p = f->type->u.func.params, v = f->paramtemps;
	     p; p = p->next, ++v) {
		if (v->kind == VALUE_NONE)
			continue;
		MVal *mv = fe_val(fn, v, tab);
		fn->paramty[pi] = -1;
		if (p->type->value) {
			/* aggregate parameter: the param temp is a Kl pointer to
			 * a stack pad; Oparc copies it in.  Record the typ[]
			 * index for the bridge and the MTypeDesc for the machine
			 * layer. */
			mv->type = MT_AGG;
			mv->td = fe_to_mtd(fn, p->type);
			fn->paramty[pi] = p->type->value->id;
		} else {
			struct irtype qt = irtype(p->type);
			MType t = fe_cls_to_mtype(qt.base);
			if (mv && t != MT_NONE)
				mv->type = t;
		}
		fn->param = realloc(fn->param,
		                    (fn->nparam + 1) * sizeof *fn->param);
		fn->param[fn->nparam++] = mv;
		pi++;
	}
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
						if (ai->arg[1] &&
						    (ai->arg[1]->kind & 0xf) == VALUE_TYPE) {
							/* aggregate argument: MOP_ARG carries the
							 * typ[] index in src[0] and the source
							 * pointer in src[1]; the bridge emits
							 * Oargc (selpar copies into the callee's
							 * stack pad or argument registers). */
							MVal *ty = fe_val(fn, ai->arg[1], tab);
							madd(fn, mb, MOP_ARG, MT_AGG, 0,
							     MREF_VAL(ty),
							     fe_ref(fn, ai->arg[0], tab));
						} else {
							madd1(fn, mb, MOP_ARG,
							      fe_cls_to_mtype(ai->class),
							      0,
							      fe_ref(fn, ai->arg[0], tab));
						}
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
				if (in->arg[1] &&
				    (in->arg[1]->kind & 0xf) == VALUE_TYPE) {
					/* aggregate return: MOP_CALL carries the typ[]
					 * index in src[1]; the bridge emits Ocall with
					 * TYPE(idx) and selcall lowers to SysV (result
					 * is a Kl pointer to the return pad). */
					MVal *ty = fe_val(fn, in->arg[1], tab);
					if (res)
						res->type = MT_PTR;
					madd(fn, mb, MOP_CALL, MT_AGG, res,
					     MREF_VAL(callee), MREF_VAL(ty));
				} else {
					MType rt = in->class ?
						fe_cls_to_mtype(in->class) : MT_NONE;
					/* The call result's MVal type must reflect the
					 * return class so the bridge can pick the right
					 * conversion (Ostosi vs Odtosi) when the result
					 * feeds a MOP_F2I/MOP_I2F.  Without this, res->type
					 * stays MT_NONE and a double->long cast after a
					 * double-returning call (e.g. `(long)addd(...)`)
					 * is lowered as stosi instead of dtosi. */
					fe_typ(res, in->class);
					madd1(fn, mb, MOP_CALL, rt, res,
					      MREF_VAL(callee));
				}
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

			/* Comparisons and float conversions carry the operand width
			 * in the frontend opcode suffix (W/L/S/D), not in the result
			 * class (which is always 'w' for int-typed comparisons).
			 * Derive the MIR dtype from the opcode so the bridge can pick
			 * the correct-width LIR op (e.g. Ocnel for a 64-bit pointer
			 * comparison instead of the 32-bit Ocnew). */
			switch (in->kind) {
			case ICEQW: case ICNEW: case ICSLEW: case ICSLTW:
			case ICSGEW: case ICSGTW: case ICULEW: case ICULTW:
			case ICUGEW: case ICUGTW:
				dt = MT_I32; break;
			case ICEQL: case ICNEL: case ICSLEL: case ICSLTL:
			case ICSGEL: case ICSGTL: case ICULEL: case ICULTL:
			case ICUGEL: case ICUGTL:
				dt = MT_I64; break;
			case ICEQS: case ICNES: case ICLES: case ICLTS:
			case ICGES: case ICGTS: case ICOS: case ICUOS:
				dt = MT_F32; break;
			case ICEQD: case ICNED: case ICLED: case ICLTD:
			case ICGED: case ICGTD: case ICOD: case ICUOD:
				dt = MT_F64; break;
			default: break;
			}

			/* stores/loads: the frontend emits the width in the opcode
			 * (ISTOREB/H/W/L/S/D, ILOAD*) and passes class 0 to the store
			 * (no result).  Derive the MIR dtype from the opcode so the
			 * bridge emits the correct-width LIR store/load. */
			switch (in->kind) {
			case ISTOREB: case ILOADUB: case ILOADSB: dt = MT_I8; break;
			case ISTOREH: case ILOADUH: case ILOADSH: dt = MT_I16; break;
			case ISTOREW: case ILOADW:                 dt = MT_I32; break;
			case ISTOREL: case ILOADL:                 dt = MT_I64; break;
			case ISTORES: case ILOADS:                 dt = MT_F32; break;
			case ISTORED: case ILOADD:                 dt = MT_F64; break;
			default: break;
			}

			/* A byte/halfword load result must carry the *data* width,
			 * not the frontend's widened class ('w'): a later SEXT of
			 * the loaded value (e.g. `(signed char)buf[0]`) selects the
			 * LIR opcode from the source operand's type, and an i32
			 * source would produce Oextsw — skipping the byte
			 * sign-extension entirely (the load's zero-extended 0xff
			 * stayed 255 instead of becoming -1). */
			if (op == MOP_LOAD && res && res->kind == MV_TEMP)
				res->type = dt;

			if (op == MOP_STORE) {
				MIns *mi = madd(fn, mb, op, dt, 0, a0, a1);
				if (in->flags & INST_VOLATILE)
					mi->extra |= MIR_VOLATILE;
			} else if (in->kind == ILOADSB || in->kind == ILOADSH) {
				/* MIR loads are width-only (the bridge emits the
				 * zero-extending opcode); the frontend's signed load
				 * opcodes carry sign extension in their meaning
				 * (ILOADSB = load byte + sign-extend), so materialize
				 * the byte/halfword load into a temp and append an
				 * explicit SEXT to the result width. */
				MVal *tmp = mval_new(fn, MV_TEMP, dt, 0, 0);
				MIns *mi = madd1(fn, mb, MOP_LOAD, dt, tmp, a0);
				if (in->flags & INST_VOLATILE)
					mi->extra |= MIR_VOLATILE;
				MType rty = fe_cls_to_mtype(in->class);
				madd1(fn, mb, MOP_SEXT, rty, res, MREF_VAL(tmp));
			} else if (a1.val || a1.con) {
				MIns *mi = madd(fn, mb, op, dt, res, a0, a1);
				if (in->flags & INST_VOLATILE)
					mi->extra |= MIR_VOLATILE;
			} else {
				MIns *mi = madd1(fn, mb, op, dt, res, a0);
				if (in->flags & INST_VOLATILE)
					mi->extra |= MIR_VOLATILE;
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

	free(fbmap);
	return fn;
}
