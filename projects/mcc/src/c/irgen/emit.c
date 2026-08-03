/* emit.c - Direct IR construction (replaces the frontend's text-IL emitter).
 *
 * The legacy reference cproc/qbe.c file emitted IR text IL via emitname/emittype/
 * emitclass/emitinst/emitjump/emitfunc/emitdata. Per AGENTS.md §2.3, that
 * text serialization step is eliminated: `emitfunc` walks the frontend's
 * `struct func` AST and builds a IR `Fn` in memory via the IR construction
 * API (newtmp / getcon / newcon / emit / idup / newblk), then runs the
 * IR pass pipeline (T.abi0 → fillcfg → ssa → … → rega) and invokes
 * T.emitfn to produce target assembly. No text IL is produced.
 *
 * Note on `fn->retty` semantics (see also project_memory.md §11):
 *   - `fn->retty` is a typ[] index used by IR ABI passes for *aggregate*
 *     returns. It defaults to -1 (Kx) and is only set non-negative when
 *     a function returns struct/union (Phase 1c+).
 *   - The local `retty` variable below tracks the SCALAR class (Kw..Kd)
 *     so the jump type can be encoded as `Jretw + retty`. Unrelated to
 *     `fn->retty`.
 *
 * `emitdata` translates a global/static object initializer into a stream
 * of IR `Dat` records emitted via `emitdat()` (DStart/DB/DH/DW/DL/DZ/DEnd),
 * covering string literals, scalar constants, and address-of-identifier
 * references. Bit-fields and wide strings are deferred to later phases. */
#include <ctype.h>
#include "irgen.h"
#include "mir.h"

extern int emit_debug;  /* from emit/emit.c */
extern void emitdbgloc(uint, uint, FILE *);  /* from emit/emit.c */
extern bool mfnm_backend_x86_64(struct MFn *);  /* x86_64_mbe.c (P3b machine backend) */

/* B.4.2 MIR-first switch: when nonzero, emitfunc lowers the frontend tree
 * to MIR (func_to_mir), runs the MIR passes, bridges to LIR, and runs the
 * LIR pipeline.  Set from MCC_USE_MIR in main.c at startup. */
int g_use_mir;

/* P2+ MIR-native backend switch: when nonzero, each function is additionally
 * run through the MIR machine backend (convert to MFnM, ABI-lower, dump).
 * The bridge path stays the default producer of assembly; this is a
 * parallel-validation hook until P3-P5 supply isel/regalloc/emit. */
int g_use_mir_backend;

/* Translate a frontend class char ('w','l','s','d') to IR's class enum. */
static int
char_to_cls(int c)
{
	switch (c) {
	case 'w': return Kw;
	case 'l': return Kl;
	case 's': return Ks;
	case 'd': return Kd;
	case  0 : return Kx;
	default:  die("mcc: invalid class '%c'", c);
	}
}

/* Translate a frontend IR op (enum instkind, IXXX) to a IR op (Oxxx).
 * The two share the same op vocabulary modulo the I/O prefix. */
static int
fe_to_ir_op(int op)
{
	switch (op) {
	/* arithmetic / bits */
	case IADD:    return Oadd;
	case ISUB:    return Osub;
	case IMUL:    return Omul;
	case IDIV:    return Odiv;
	case IUDIV:   return Oudiv;
	case IREM:    return Orem;
	case IUREM:   return Ourem;
	case INEG:    return Oneg;
	case IAND:    return Oand;
	case IOR:     return Oor;
	case IXOR:    return Oxor;
	case ISAR:    return Osar;
	case ISHR:    return Oshr;
	case ISHL:    return Oshl;
	/* sign/zero extensions */
	case IEXTUB:  return Oextub;
	case IEXTSB:  return Oextsb;
	case IEXTUH:  return Oextuh;
	case IEXTSH:  return Oextsh;
	case IEXTUW:  return Oextuw;
	case IEXTSW:  return Oextsw;
	/* float conversions */
	case IEXTS:   return Oexts;
	case ITRUNCD: return Otruncd;
	case ISTOSI:  return Ostosi;
	case ISTOUI:  return Ostoui;
	case IDTOSI:  return Odtosi;
	case IDTOUI:  return Odtoui;
	case ISWTOF:  return Oswtof;
	case IUWTOF:  return Ouwtof;
	case ISLTOF:  return Osltof;
	case IULTOF:  return Oultof;
	/* cast / copy */
	case ICAST:   return Ocast;
	case ICOPY:   return Ocopy;
	/* memory store */
	case ISTOREB: return Ostoreb;
	case ISTOREH: return Ostoreh;
	case ISTOREW: return Ostorew;
	case ISTOREL: return Ostorel;
	case ISTORES: return Ostores;
	case ISTORED: return Ostored;
	/* memory load (frontend distinguishes width by op; IR's Oload takes
	 * width from the instruction's cls field, so all map to Oload except
	 * the sign/zero-extending byte/halfword loads). */
	case ILOADUB: return Oloadub;
	case ILOADSB: return Oloadsb;
	case ILOADUH: return Oloaduh;
	case ILOADSH: return Oloadsh;
	case ILOADW:  return Oload;   /* 32-bit load (cls=Kw) */
	case ILOADL:  return Oload;   /* 64-bit load (cls=Kl) */
	case ILOADS:  return Oload;   /* float load (cls=Ks) */
	case ILOADD:  return Oload;   /* double load (cls=Kd) */
	/* stack alloc */
	case IALLOC4:  return Oalloc4;
	case IALLOC8:  return Oalloc8;
	case IALLOC16: return Oalloc16;
	/* comparisons (w) */
	case ICEQW: return Oceqw;
	case ICNEW: return Ocnew;
	case ICSLEW:return Ocslew;
	case ICSLTW:return Ocsltw;
	case ICSGEW:return Ocsgew;
	case ICSGTW:return Ocsgtw;
	case ICULEW:return Oculew;
	case ICULTW:return Ocultw;
	case ICUGEW:return Ocugew;
	case ICUGTW:return Ocugtw;
	/* comparisons (l) */
	case ICEQL: return Oceql;
	case ICNEL: return Ocnel;
	case ICSLEL:return Ocslel;
	case ICSLTL:return Ocsltl;
	case ICSGEL:return Ocsgel;
	case ICSGTL:return Ocsgtl;
	case ICULEL:return Oculel;
	case ICULTL:return Ocultl;
	case ICUGEL:return Ocugel;
	case ICUGTL:return Ocugtl;
	/* comparisons (s) */
	case ICEQS: return Oceqs;
	case ICNES: return Ocnes;
	case ICLES: return Ocles;
	case ICLTS: return Oclts;
	case ICGES: return Ocges;
	case ICGTS: return Ocgts;
	case ICOS:  return Ocos;
	case ICUOS: return Ocuos;
	/* comparisons (d) */
	case ICEQD: return Oceqd;
	case ICNED: return Ocned;
	case ICLED: return Ocled;
	case ICLTD: return Ocltd;
	case ICGED: return Ocged;
	case ICGTD: return Ocgtd;
	case ICOD:  return Ocod;
	case ICUOD: return Ocuod;
	/* variadic */
	case IVASTART: return Ovastart;
	case IVAARG:   return Ovaarg;
	default:
		die("mcc: IR op %d not mapped to IR", op);
	}
}

/* Translate a frontend `struct value *` to a IR `Ref`.
 * For VALUE_TEMP we assume temp IDs were pre-allocated in emitfunc
 * (IR tmp index = Tmp0 + v->id - 1, since frontend IDs start at 1). */
static Ref
valref(struct value *v, Fn *fn)
{
	Con c;
	char *fullname;

	if (!v)
		return R;
	switch (v->kind & 0xf) {
	case VALUE_TEMP:
		return TMP(Tmp0 + v->id - 1);
	case VALUE_INTCONST:
		return getcon((int64_t)v->u.i, fn);
	case VALUE_FLTCONST:
		memset(&c, 0, sizeof c);
		c.type = CBits;
		c.flt = 1;
		c.bits.s = (float)v->u.f;
		return newcon(&c, fn);
	case VALUE_DBLCONST:
		memset(&c, 0, sizeof c);
		c.type = CBits;
		c.flt = 2;
		c.bits.d = v->u.f;
		return newcon(&c, fn);
	case VALUE_GLOBAL:
		/* The legacy emitter printed "$[.L]name[.id]"; reconstruct the
		 * bare global identifier that IR's intern() expects (no $
		 * sigil). Static globals (LINKNONE) carry a positive id and
		 * get a ".L<name>.<id>" symbol; externs use the bare name. */
		if (v->id)
			fullname = strf(PFn, ".L%s.%u", v->u.name, v->id);
		else
			fullname = v->u.name;
		memset(&c, 0, sizeof c);
		c.type = CAddr;
		c.sym.id = intern(fullname);
		if (v->kind & VALUE_THREAD) {
			switch (tls_model) {
			case TLSM_GLOBAL_DYNAMIC:
				c.sym.type = SGenThr;
				break;
			case TLSM_INITIAL_EXEC:
				c.sym.type = SExt | SThr;
				break;
			case TLSM_LOCAL_EXEC:
				c.sym.type = SThr;
				break;
			case TLSM_DEFAULT:
			default:
				if (v->kind & VALUE_EXTERN && T.pic)
					c.sym.type = SGenThr;
				else if (v->kind & VALUE_EXTERN)
					c.sym.type = SExt | SThr;
				else
					c.sym.type = SThr;
				break;
			}
		} else {
			if (v->kind & VALUE_EXTERN) c.sym.type |= SExt;
		}
		return newcon(&c, fn);
	case VALUE_TYPE:
		/* IR encodes aggregate-type references as TYPE(n) where n is the
		 * typ[] index; emittype() assigned v->id when it built the Typ. */
		return TYPE(v->id);
	default:
		die("mcc: unsupported value kind 0x%x", v->kind & 0xf);
	}
}

/* If `r` references an SGenThr constant, emit Oarg + Ocall(__tls_get_addr)
 * and return the result temp (the TLS address). Otherwise return r unchanged. */
static Ref
expand_gd_tls(Ref r, Fn *fn)
{
	Con *c;
	if (rtype(r) != RCon)
		return r;
	c = &fn->con[r.val];
	if (c->type != CAddr || c->sym.type != SGenThr)
		return r;

	Con cc;
	memset(&cc, 0, sizeof cc);
	cc.type = CAddr;
	cc.sym.id = intern("__tls_get_addr");
	cc.sym.type = SExt;

	Ref result = newtmp("tlsgd", Kl, fn);
	*curi++ = (Ins){.op = Oarg, .cls = Kl, .to = R, .arg = {r, R}};
	*curi++ = (Ins){.op = Ocall, .cls = Kl, .to = result, .arg = {newcon(&cc, fn), R}};
	fn->leaf = 0;
	return result;
}

void
emitfunc(struct func *f, bool global)
{
	struct block *b;
	struct inst **instp, **instend;
	struct inst *in;
	Fn *fn;
	int retty;
	uint nblk = 0;
	int *tempcls;

	/* C99 5.1.2.2.3: implicit `return 0;` from main */
	if (f->end->jump.kind == JUMP_NONE) {
		struct value *v = NULL;
		if (strcmp(f->name, "main") == 0 && f->type->base == &typeint)
			v = mkintconst(0);
		funcret(f, v);
	}

	/* B.4.2 MIR-first path: lower the frontend tree to MIR, run the MIR
	 * passes, bridge to the LIR Fn, and run the LIR pipeline.  Enabled by
	 * MCC_USE_MIR=1 (kept opt-in while the MIR layer is being validated
	 * against real C workloads; the default remains the direct-LIR path
	 * until the migration completes). */
	if (g_use_mir) {
		MFn *mf = func_to_mir(f, opt_level, global);
		if (mf) {
			if (emit_debug) {
				fprintf(stderr, "\n> MIR (pre-pass) %s:\n", f->name);
				mfn_dump(mf, stderr);
			}
			run_mir_passes(mf, opt_level);
			if (emit_debug) {
				fprintf(stderr, "\n> MIR (post-pass) %s:\n", f->name);
				mfn_dump(mf, stderr);
			}
			/* P3b MIR-native backend: when MCC_MIR_BACKEND=1, the machine
			 * backend emits the assembly for functions in scope (scalar);
			 * otherwise (aggregate/varargs) it falls back to the bridge. */
			if (g_use_mir_backend && mfnm_backend_x86_64(mf)) {
				freeall();
				mfn_free(mf);
				return;
			}
			fn = lir_bridge(mf);
			if (emit_debug) {
				fprintf(stderr, "\n> LIR (bridged) %s:\n", f->name);
				printfn(fn, stderr);
			}
			run_passes(fn);
			if (emit_debug)
				emitdbgloc(1, 0, stdout);
			T.emitfn(fn, stdout);
			freeall();
			mfn_free(mf);
			return;
		}
		/* fall through to direct-LIR path if MIR lowering failed */
	}

	/* Allocate Fn per the qbe parse.c L910-932 init pattern. */
	fn = alloc(sizeof *fn);
	fn->ntmp = 0;
	fn->ncon = 2;
	fn->nmem = 0;
	fn->tmp = vnew(fn->ntmp, sizeof fn->tmp[0], PFn);
	fn->con = vnew(fn->ncon, sizeof fn->con[0], PFn);
	fn->mem = vnew(0, sizeof fn->mem[0], PFn);
	/* Physical-register placeholder tmps [0..Tmp0-1]. */
	for (int i = 0; i < Tmp0; i++) {
		if (T.fpr0 <= i && i < T.fpr0 + T.nfpr)
			newtmp(0, Kd, fn);
		else
			newtmp(0, Kl, fn);
	}
	/* con[0] = UNDEF, con[1] = 0 (CON_Z). */
	fn->con[0].type = CBits;
	fn->con[0].bits.i = 0xdeaddead;
	fn->con[1].type = CBits;
	fn->con[1].bits.i = 0;

	/* Fn name + linkage. */
	fn->name = f->name;
	fn->lnk = (Lnk){0};
	fn->lnk.export = global;
	fn->leaf = 1;
	fn->vararg = f->type->u.func.isvararg;
	fn->optlevel = opt_level;
	fn->warnlevel = warn_level;
	/* `slot` counts the stack frame size in 4-byte units; the IR
	 * parser zero-inits it via alloc() (parse.c parsefn). mcc builds
	 * Fn directly so we must match: 0, not -1. Leaving it negative
	 * makes amd64_isel.c's `sz > INT_MAX - fn->slot` overflow check
	 * trip on the first Oalloc4 emitted by sysv.c for an in-reg
	 * aggregate parameter. */
	fn->slot = 0;
	fn->salign = 0;
	fn->dynalloc = 0;
	fn->retr = R;

	/* Return type → fn->retty + local class. See file header note. */
	if (f->type->base == &typevoid) {
		fn->retty = -1;
		retty = -1;
	} else if (f->type->base->value) {
		/* aggregate (struct/union) return: fn->retty is the typ[] index
		 * (set by emittype, called from mkfunc). The return jump uses
		 * Jretc, not Jretw+retty, so the local retty is left unused. */
		fn->retty = f->type->base->value->id;
		retty = -1;
	} else {
		fn->retty = -1;
		retty = char_to_cls(irtype(f->type->base).base);
	}

	/* Pre-pass: collect temp classes from defining instructions.
	 * The frontend assigns sequential IDs starting at 1; IR tmps live
	 * at index Tmp0..Tmp0+lastid-1. Pre-allocate them with the right
	 * cls so valref()'s TMP(Tmp0+v->id-1) mapping is consistent. */
	tempcls = xmalloc(sizeof(*tempcls) * (f->lastid + 1));
	for (unsigned i = 0; i <= f->lastid; i++)
		tempcls[i] = Kx;
	for (b = f->start; b; b = b->next) {
		instend = (struct inst **)((char *)b->insts.val + b->insts.len);
		for (instp = b->insts.val; instp != instend; ++instp) {
			in = *instp;
			if (in->res.kind == VALUE_TEMP && in->class)
				tempcls[in->res.id] = char_to_cls(in->class);
		}
		/* Phi result temps are assigned via functemp but not via
		 * inst->res, so they aren't caught by the loop above. */
		if (b->phi.res.kind == VALUE_TEMP)
			tempcls[b->phi.res.id] = char_to_cls(b->phi.class);
	}
	/* Parameter temps are not defined by any instruction in the body —
	 * they're implicitly defined by Opar at function entry (emitted in
	 * Pass 2 below). Set their class here so newtmp() pre-allocates
	 * them with the right IR class. */
	{
		struct decl *p;
		struct value *v;
		for (p = f->type->u.func.params, v = f->paramtemps;
		     p; p = p->next, ++v) {
			tempcls[v->id] = char_to_cls(irtype(p->type).base);
		}
	}
	for (unsigned i = 1; i <= f->lastid; i++)
		newtmp(0, tempcls[i], fn);

	/* Pass 1: create a IR Blk for each frontend block, store the
	 * mapping in b->ir for Pass 2 to consume when resolving jump
	 * targets. Also chain all blocks via Blk->link — fillrpo()/fillcfg()
	 * traverse this chain to enumerate reachable blocks. */
	{
		Blk *prev = NULL;
		for (b = f->start; b; b = b->next) {
			b->ir = newblk();
			b->ir->id = nblk++;
			b->ir->name = strf(PFn, "%s", b->label.u.name);
			b->ir->loop = 0;
			b->ir->link = NULL;
			if (prev)
				prev->link = b->ir;
			prev = b->ir;
		}
	}
	fn->nblk = nblk;
	fn->start = f->start->ir;
	/* rpo will be (re)sized by fillrpo, but fillcfg expects a valid Vec
	 * pointer on entry — initialize to a zero-length Vec. */
	fn->rpo = vnew(nblk, sizeof fn->rpo[0], PFn);

	/* Pass 2: translate instructions and jumps.
	 *
	 * Unlike IR optimization passes (which use BACKWARD emit() because
	 * they regenerate instructions in reverse), here we CONSTRUCT initial
	 * IR — so we use FORWARD writes (`*curi++ = (Ins){...}`) matching
	 * IR's parse.c convention (the parser also builds IR forward). This
	 * produces instructions in source order without the inversion that
	 * pairing BACKWARD emit() with forward iteration would cause.
	 *
	 * The shared `curi`/`insb` buffer is large (NIns = 1<<20); IR passes
	 * later reset `curi = &insb[NIns]` and use emit() independently. */
	curi = insb;
	for (b = f->start; b; b = b->next) {
		Blk *qb = b->ir;

		/* For the entry block, emit Opar instructions FIRST (FORWARD
		 * writes put first-emitted at the lowest position, so Opar
		 * lands at the start of qb->ins). The frontend creates param
		 * temps (mkfunc, func.c L33) and stores them into stack slots,
		 * but never emits a defining instruction — that was implicit
		 * in the frontend's text IL via the function signature
		 * `function w $foo(w %.1, ...)`. In direct IR mode we must
		 * emit Opar explicitly or ssacheck reports the param as
		 * "used undefined". See qbe/parse.c parserefl() for the
		 * parser-side equivalent. */
		if (b == f->start) {
			struct decl *p;
			struct value *v;
			for (p = f->type->u.func.params, v = f->paramtemps;
			     p; p = p->next, ++v) {
				struct irtype qt = irtype(p->type);
				int cls = char_to_cls(qt.base);
				if (p->type->value) {
					/* aggregate parameter: Oparc lowers to a stack
					 * copy the callee addresses by value. arg[0]
					 * carries TYPE(idx) (read by argsclass); the
					 * param temp (a Kl pointer) is the stack pad. */
					*curi++ = (Ins){.op = Oparc, .cls = Kl,
					                .to = valref(v, fn),
					                .arg = {TYPE(p->type->value->id), R}};
				} else {
					*curi++ = (Ins){.op = Opar, .cls = cls,
					                .to = valref(v, fn), .arg = {R, R}};
				}
			}
		}

		/* Translate frontend binary phi to a IR Phi node and link it
		 * as the head of qb->phi. The frontend only emits 2-arg
		 * phis (one per merge block) for short-circuit logic / if-else
		 * result merging, so a single Phi node suffices. */
		if (b->phi.res.kind != VALUE_NONE) {
			Phi *phi = alloc(sizeof *phi);
			phi->to = valref(&b->phi.res, fn);
			phi->cls = char_to_cls(b->phi.class);
			phi->visit = 0;
			phi->narg = 2;
			phi->arg = vnew(2, sizeof phi->arg[0], PFn);
			phi->arg[0] = valref(b->phi.val[0], fn);
			phi->arg[1] = valref(b->phi.val[1], fn);
			phi->blk = vnew(2, sizeof phi->blk[0], PFn);
			phi->blk[0] = b->phi.blk[0]->ir;
			phi->blk[1] = b->phi.blk[1]->ir;
			phi->link = NULL;
			qb->phi = phi;
		}

		/* instructions — FORWARD writes in source order */
		instend = (struct inst **)((char *)b->insts.val + b->insts.len);
		for (instp = b->insts.val; instp != instend; ++instp) {
			in = *instp;

			/* IARG/IVARARG outside ICALL context — defensive skip
			 * (shouldn't happen since ICALL handler consumes them). */
			if (in->kind == IARG || in->kind == IVARARG)
				continue;

			if (in->kind == ICALL) {
				/* ICALL layout (from funcexpr, expr.c L76-82):
				 *   ICALL  cls=ret  res=retval  arg[0]=callee  arg[1]=functype
				 *   [IVARARG]                      (varargs marker)
				 *   IARG   cls=argtype  arg[0]=argval
				 *   ...
				 *
				 * IR order: Oarg*, [Oargv], Ocall.
				 * FORWARD writes produce this directly (in
				 * source order). See qbe/parse.c L712-725. */
				Ref callee = valref(in->arg[0], fn);
				Ref result = in->res.kind ? valref(&in->res, fn) : R;
				Ref call_arg1 = R;
				int call_cls;
				struct inst **argp = instp + 1;

				/* Aggregate return: arg[1] is the VALUE_TYPE for the
				 * returned struct/union. IR encodes this as the call's
				 * second operand TYPE(idx) and uses cls=Kl (the result
				 * is a pointer to the return pad). selcall lowers the
				 * hidden-pointer / register-copy sequence. parse.c L718. */
				if (in->arg[1] && (in->arg[1]->kind & 0xf) == VALUE_TYPE) {
					call_cls = Kl;
					call_arg1 = valref(in->arg[1], fn);
				} else {
					call_cls = in->class ? char_to_cls(in->class) : Kw;
					call_arg1 = R;
				}

				while (argp != instend) {
					struct inst *ai = *argp;
					if (ai->kind == IARG) {
						if (ai->arg[1] && (ai->arg[1]->kind & 0xf) == VALUE_TYPE) {
							/* aggregate argument: Oargc copies from
							 * the source pointer. arg[0]=TYPE(idx)
							 * (read by argsclass), arg[1]=source ptr
							 * (used by selcall). parse.c L717. */
							*curi++ = (Ins){
								.op = Oargc, .cls = Kl,
								.to = R,
							.arg = {TYPE(ai->arg[1]->id),
							        expand_gd_tls(valref(ai->arg[0], fn), fn)}
							};
						} else {
							*curi++ = (Ins){
								.op = Oarg,
								.cls = char_to_cls(ai->class),
								.to = R,
								.arg = {expand_gd_tls(valref(ai->arg[0], fn), fn), R}
							};
						}
					} else if (ai->kind == IVARARG) {
						*curi++ = (Ins){
							.op = Oargv, .cls = 0,
							.to = R, .arg = {R, R}
						};
					} else {
						break;
					}
					argp++;
				}
				*curi++ = (Ins){
					.op = Ocall, .cls = call_cls,
					.to = result, .arg = {callee, call_arg1}
				};
				instp = argp - 1;
				fn->leaf = 0;
				continue;
			}

			int qop = fe_to_ir_op(in->kind);
			int cls = char_to_cls(in->class);

			/* Store instructions have no result, so the frontend records no
			 * result class for them.  Their data class is nevertheless fixed
			 * by the opcode.  Leaving it as Kx lets later IR passes infer an
			 * arbitrary class from the address operand; a VLA store can then
			 * reach a RISC selector as (for example) Ostorew(Kd). */
			if (cls == Kx)
				switch (qop) {
				case Ostoreb:
				case Ostoreh:
				case Ostorew: cls = Kw; break;
				case Ostorel: cls = Kl; break;
				case Ostores: cls = Ks; break;
				case Ostored: cls = Kd; break;
				}
		Ref to = in->res.kind ? valref(&in->res, fn) : R;
		Ref a0 = in->arg[0] ? valref(in->arg[0], fn) : R;
		Ref a1 = in->arg[1] ? valref(in->arg[1], fn) : R;
		a0 = expand_gd_tls(a0, fn);
		a1 = expand_gd_tls(a1, fn);
		*curi++ = (Ins){.op = qop, .cls = cls, .to = to, .arg = {a0, a1}};
		}

		/* Pre-evaluate jump args that need GD-TLS expansion BEFORE
		 * idup/reset, so the Oarg+Ocall they emit get committed to
		 * qb->ins (valref() alone would orphan them in insb[0..1]). */
		Ref retval = R;   /* for JUMP_RET */
		Ref jnz_val = R;  /* for JUMP_JNZ */
		if (b->jump.kind == JUMP_RET && b->jump.arg)
			retval = expand_gd_tls(valref(b->jump.arg, fn), fn);
		if (b->jump.kind == JUMP_JNZ && b->jump.arg)
			jnz_val = expand_gd_tls(valref(b->jump.arg, fn), fn);

		idup(qb, insb, curi - insb);
		curi = insb;

		/* jump */
		switch (b->jump.kind) {
		case JUMP_NONE:
			/* Frontend relies on block ordering for fallthrough. */
			qb->jmp.type = Jjmp;
			qb->s1 = b->next ? b->next->ir : 0;
			break;
		case JUMP_RET:
			if (b->jump.arg) {
				if (f->type->base->value) {
					/* aggregate return: Jretc uses the typ[] entry
					 * at fn->retty; selret lowers the copy-out. */
					qb->jmp.type = Jretc;
				} else {
					/* Jretw + retty works for retty in {0..3}
					 * (Kw..Kd).  Void returns take Jret0 below. */
					qb->jmp.type = Jretw + retty;
				}
				qb->jmp.arg = retval;
			} else {
				qb->jmp.type = Jret0;
			}
			break;
		case JUMP_JMP:
			qb->jmp.type = Jjmp;
			qb->s1 = b->jump.blk[0]->ir;
			break;
		case JUMP_JNZ:
			qb->jmp.type = Jjnz;
			qb->jmp.arg = jnz_val;
			qb->s1 = b->jump.blk[0]->ir;
			qb->s2 = b->jump.blk[1]->ir;
			break;
		case JUMP_HLT:
			qb->jmp.type = Jhlt;
			break;
		default:
			assert(0);
		}
	}

	/* Run optimization + codegen pipeline, then emit asm. */
	run_passes(fn);
	if (emit_debug)
		emitdbgloc(1, 0, stdout);  /* .loc 1 line col (line=1 as placeholder) */
	T.emitfn(fn, stdout);
	freeall();
}

/* Return the assembly symbol name for a global value. Mirrors valref()'s
 * VALUE_GLOBAL handling: static globals (LINKNONE, positive id) get
 * ".L<name>.<id>", externs use the bare name, and asm-quoted names are
 * wrapped in double quotes (emitdat/emitlnk suppress T.assym for names
 * beginning with '"'). */
static char *
globalname(struct value *v)
{
	const char *name = v->u.name ? v->u.name : "compound";

	if (v->kind & VALUE_QUOTE)
		return strf(PFn, "\"%s\"", name);
	if (v->id)
		return strf(PFn, ".L%s.%u", name, v->id);
	return (char *)name;
}

/* Build a double-quoted, C-escaped representation of data[0..size) for
 * emission as a IR `DB`/`.ascii` Dat. Printable bytes (except '"' and '\\')
 * are copied verbatim; everything else uses '\%03o' (matching the frontend's
 * dataitem escape rule, which GAS interprets in .ascii). */
static char *
escape_string(unsigned char *data, size_t size)
{
	struct array buf = {0};
	char term = '\0';

	arrayaddbuf(&buf, "\"", 1);
	for (size_t i = 0; i < size; ++i) {
		unsigned char c = data[i];
		if (isprint(c) && c != '"' && c != '\\') {
			char ch = c;
			arrayaddbuf(&buf, &ch, 1);
		} else {
			char oct[5];
			snprintf(oct, sizeof oct, "\\%03o", c);
			arrayaddbuf(&buf, oct, strlen(oct));
		}
	}
	arrayaddbuf(&buf, "\"", 1);
	arrayaddbuf(&buf, &term, 1);
	return buf.val;  /* heap-allocated via xreallocary inside arrayaddbuf */
}

/* Emit one integer-sized storage unit used by static data, selecting the
 * direct-IR data record from its byte width. */
static void
emitintegerdata(Dat *dat, unsigned long long size, unsigned long long value)
{
	dat->isstr = 0;
	dat->isref = 0;
	dat->u.num = value;
	switch (size) {
	case 1: dat->type = DB; break;
	case 2: dat->type = DH; break;
	case 4: dat->type = DW; break;
	case 8: dat->type = DL; break;
		default:
		err("unsupported bit-field storage size %llu", size);
	}
}

void
emitdata(struct decl *d, struct init *init)
{
	struct init *cur;
	struct type *t;
	unsigned long long offset = 0, start, end;
	Dat dat;
	Lnk lnk = {0};

	/* Fold all initializer expressions to constants first. */
	for (cur = init; cur; cur = cur->next)
		cur->expr = eval(cur->expr);

	/* Build linkage from decl attributes. */
	lnk.export = (d->linkage == LINKEXTERN);
	lnk.thread = (d->kind == DECLOBJECT && d->u.obj.storage == SDTHREAD);
	lnk.align = d->u.obj.align;

	/* DStart initializes emitdat's zero-accumulator; name/lnk are picked
	 * up from the first actual data record (the local `dat` is reused, so
	 * they persist across the loop). */
	dat.type = DStart;
	dat.name = globalname(d->value);
	dat.lnk = &lnk;
	emitdat(&dat, stdout);

	/* `init` is assumed sorted by start offset (parseinit emits it that
	 * way). Walk it, gap-filling with zeros between non-contiguous ranges. */
	while (init) {
		cur = init;
		init = init->next;

		/* Adjacent initializers for fields in one bit-field storage unit
		 * describe disjoint bit ranges but share the same bytes.  Pack them
		 * before emitting data, otherwise each initializer would overwrite
		 * the preceding one.  `before` is the number of lower-order bits
		 * preceding the field in the storage unit; funcbits() removes the
		 * complementary high-order (`after`) bits while reading it. */
		if (cur->bits.before || cur->bits.after) {
			unsigned long long value = 0;
			unsigned long long unit_start = cur->start;
			unsigned long long unit_end = cur->end;
			unsigned long long unit_bits = (unit_end - unit_start) * 8;

			for (;;) {
				unsigned long long width, mask;
				if (cur->expr->kind != EXPRCONST
				 || !(cur->expr->type->prop & PROPINT))
					error(&tok.loc, "bit-field initializer is not an integer constant expression");
				width = unit_bits - cur->bits.before - cur->bits.after;
				mask = width == 64 ? ~0ull : (1ull << width) - 1;
				value |= (cur->expr->u.constant.u & mask) << cur->bits.before;
				if (!init || !(init->bits.before || init->bits.after)
				 || init->start != unit_start || init->end != unit_end)
					break;
				cur = init;
				init = init->next;
			}
			/* The storage unit can begin inside a byte range already
			 * emitted by an earlier non-bit-field member that shares the
			 * unit (e.g. `char a; int b : 5;` — b's 4-byte unit starts at
			 * byte 0 and overlaps a's byte 0).  funcstore() preserves the
			 * overlapping member's bits via masked insert; emitdata must
			 * mirror that by right-shifting the packed value to the
			 * still-unemitted part of the unit instead of re-emitting the
			 * whole unit and clobbering the earlier member. */
			if (offset > unit_start) {
				value >>= (offset - unit_start) * 8;
				unit_start = offset;
			}
			if (offset < unit_start) {
				dat.type = DZ;
				dat.isstr = 0;
				dat.isref = 0;
				dat.u.num = unit_start - offset;
				emitdat(&dat, stdout);
			}
			/* emit the remaining bytes (width need not be 1/2/4/8). */
			{
				unsigned long long len = unit_end - unit_start;
				if (len >= 8) {
					emitintegerdata(&dat, 8, value & ~0ull);
					emitdat(&dat, stdout);
					len -= 8;
					value = 0;   /* the packed unit is at most 64 bits */
				}
				while (len >= 4) {
					emitintegerdata(&dat, 4, value & 0xffffffffull);
					emitdat(&dat, stdout);
					value >>= 32;
					len -= 4;
				}
				while (len >= 2) {
					emitintegerdata(&dat, 2, value & 0xffffull);
					emitdat(&dat, stdout);
					value >>= 16;
					len -= 2;
				}
				if (len == 1) {
					emitintegerdata(&dat, 1, value & 0xffull);
					emitdat(&dat, stdout);
				}
			}
			offset = unit_end;
			continue;
		}

		start = cur->start + cur->bits.before / 8;
		end = cur->end - (cur->bits.after + 7) / 8;

		/* gap fill */
		if (offset < start) {
			dat.type = DZ;
			dat.isstr = 0;
			dat.isref = 0;
			dat.u.num = start - offset;
			emitdat(&dat, stdout);
			offset = start;
		}

		/* For arrays the element type drives the data width. */
		t = cur->expr->type;
		if (t->kind == TYPEARRAY)
			t = t->base;

		dat.isstr = 0;
		dat.isref = 0;
		memset(&dat.u, 0, sizeof dat.u);

		switch (cur->expr->kind) {
		case EXPRSTRING: {
			size_t w = cur->expr->type->base->size;
			size_t slen, actual;
			if (w != 1)
				die("mcc: wide string initializer not yet supported");
			slen = cur->end - cur->start;
			actual = cur->expr->u.string.size < slen
				? cur->expr->u.string.size : slen;
			dat.type = DB;
			dat.isstr = 1;
			dat.u.str = escape_string(
				(unsigned char *)cur->expr->u.string.data, actual);
			emitdat(&dat, stdout);
			free(dat.u.str);
			/* Zero-fill the remainder of the init range when the
			 * literal is shorter than its declared slot. */
			if (actual < slen) {
				dat.isstr = 0;
				dat.type = DZ;
				dat.u.num = slen - actual;
				emitdat(&dat, stdout);
			}
			break;
		}
		case EXPRCONST:
			if (t->prop & PROPFLOAT) {
				if (t->size == 4) {
					dat.type = DW;
					dat.u.flts = (float)cur->expr->u.constant.f;
				} else {
					dat.type = DL;
					dat.u.fltd = cur->expr->u.constant.f;
				}
			} else {
				switch (irtype(t).data) {
				case 'b': dat.type = DB; break;
				case 'h': dat.type = DH; break;
				case 'w': dat.type = DW; break;
				case 'l': dat.type = DL; break;
				default:
					die("mcc: unsupported int data width '%c'",
						irtype(t).data);
				}
				dat.u.num = cur->expr->u.constant.u;
			}
			emitdat(&dat, stdout);
			break;
		case EXPRUNARY:
		case EXPRBINARY: {
			/* Address-of-identifier, optionally + constant offset:
			 *   &x      -> DL ref { name=x, off=0 }
			 *   &x + N  -> DL ref { name=x, off=N }
			 * Lowered as a single IR `DL`/`.quad name+off` Dat on
			 * LP64, or `DW`/`.int name+off` on ILP32 (i386) where
			 * pointers are 4 bytes. `.quad` is unsupported by
			 * `as --32` (BFD_RELOC_64), so the width must track
			 * the target pointer size. */
			struct expr *base;
			struct decl *refdecl;
			long long off = 0;

			if (cur->expr->kind == EXPRBINARY) {
				if (cur->expr->op != TADD
				    || cur->expr->u.binary.l->kind != EXPRUNARY
				    || cur->expr->u.binary.l->op != TBAND
				    || cur->expr->u.binary.r->kind != EXPRCONST)
					error(&tok.loc, "initializer is not a constant expression");
				base = cur->expr->u.binary.l;
				off = (long long)cur->expr->u.binary.r->u.constant.i;
			} else {
				if (cur->expr->op != TBAND)
					error(&tok.loc, "initializer is not a constant expression");
				base = cur->expr;
			}
			if (base->base->kind != EXPRIDENT)
				error(&tok.loc, "initializer is not a constant expression");
			refdecl = base->base->u.ident.decl;
			if (refdecl->kind == DECLOBJECT
			    && refdecl->u.obj.storage != SDSTATIC
			    && refdecl->linkage != LINKEXTERN
			    && refdecl->linkage != LINKINTERN)
				error(&tok.loc, "initializer is not a constant expression");
			dat.type = typelong.size == 4 ? DW : DL;
			dat.isref = 1;
			dat.u.ref.name = globalname(refdecl->value);
			dat.u.ref.off = off;
			emitdat(&dat, stdout);
			break;
		}
		default:
			error(&tok.loc, "initializer is not a constant expression");
		}
		offset = end;
	}

	/* Zero-fill up to the declared object size. */
	if (offset < d->type->size) {
		dat.isstr = 0;
		dat.isref = 0;
		dat.type = DZ;
		dat.u.num = d->type->size - offset;
		emitdat(&dat, stdout);
	}

	dat.type = DEnd;
	dat.lnk = &lnk;
	emitdat(&dat, stdout);
}
