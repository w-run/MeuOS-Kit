#include "i386.h"
#include <string.h>

/* i386 cdecl ABI: all arguments passed on stack.
 *
 * Stack layout at function entry (after call):
 *   [esp]    = return address
 *   [esp+4]  = arg0
 *   [esp+8]  = arg1
 *   ...
 *
 * After pushl %ebp; movl %esp, %ebp:
 *   [ebp]    = old ebp
 *   [ebp+4]  = return address
 *   [ebp+8]  = arg0   (slot -2)
 *   [ebp+12] = arg1   (slot -3)
 *   ...
 *
 * Return value: EAX (32-bit), EDX:EAX (64-bit).
 * Struct return: caller passes buffer pointer as
 *   hidden first argument; callee returns it in EAX.
 */

typedef struct AClass AClass;
typedef struct RAlloc RAlloc;

struct AClass {
	Typ *type;
	int inmem;
	int align;
	uint size;
	int cls[2];
	Ref ref[2];
};

struct RAlloc {
	Ins i;
	RAlloc *link;
};

/* layout of call's second argument (RCall)
 *
 *  15    4  3  2  1  0
 *  |xxxxx|xx|xx|xx|xx|              range
 *        |   |   |  ` gp regs returned (0..2)
 *        |   |   ` unused (0)
 *        |   ` unused (0)
 *        ` stack bytes / 4
 */

int i386_sysv_rsave[] = {
	EAX, ECX, EDX, -1
};
int i386_sysv_rclob[] = {EBX, ESI, EDI, -1};

MAKESURE(sysv_arrays_ok,
	sizeof i386_sysv_rsave == (NGPS+1) * sizeof(int) &&
	sizeof i386_sysv_rclob == (NCLR+1) * sizeof(int)
);

bits
i386_sysv_retregs(Ref r, int p[2])
{
	bits b;
	int ni;

	assert(rtype(r) == RCall);
	b = 0;
	ni = r.val & 3;
	if (ni >= 1)
		b |= BIT(EAX);
	if (ni >= 2)
		b |= BIT(EDX);
	if (p) {
		p[0] = ni;
		p[1] = 0;
	}
	return b;
}

bits
i386_sysv_argregs(Ref r, int p[2])
{
	/* i386 cdecl: no registers used for arguments */
	assert(rtype(r) == RCall);
	if (p) {
		p[0] = 0;
		p[1] = 0;
	}
	return 0;
}

static void
typclass(AClass *a, Typ *t)
{
	uint sz, al;

	sz = t->size;
	al = 1u << t->align;

	/* round size up to 4-byte boundary */
	if (al < 4)
		al = 4;
	sz = (sz + al-1) & -al;

	a->type = t;
	a->size = sz;
	a->align = t->align;
	a->inmem = 1;  /* all structs in memory on i386 */
	a->cls[0] = Kl;
	a->cls[1] = Kl;
}

static void
selret(Blk *b, Fn *fn)
{
	int j, k, ca;
	Ref r0, r1;
	AClass aret;

	j = b->jmp.type;

	if (!isret(j) || j == Jret0)
		return;

	r0 = b->jmp.arg;
	b->jmp.type = Jret0;

	if (j == Jretc) {
		typclass(&aret, &typ[fn->retty]);
		assert(rtype(fn->retr) == RTmp);
		/* struct return: hidden return pointer is Kw on i386
		 * (ILP32: pointers are 4 bytes, not Kl). */
		emit(Ocopy, Kw, TMP(EAX), fn->retr, R);
		emit(Oblit1, 0, R, INT(aret.type->size), R);
		emit(Oblit0, 0, R, r0, fn->retr);
		ca = 1;
	} else {
		k = j - Jretw;
		if (KBASE(k) == 0) {
			if (k == Kl) {
				/* 64-bit return: EDX:EAX pair.
				 *
				 * Convention: a Kl-class Ocopy with TMP(EAX) as
				 * one operand denotes the EAX:EDX register pair
				 * (low:high). emit's Ocopy Kl case decomposes this
				 * into the right movl pair, reading/writing the
				 * other operand's slot (Kl temps never get a
				 * register because kl_in_reg=0).
				 *
				 * ca = 2 so spill.c adds both EAX and EDX to the
				 * return block's live register set v. The Ocopy
				 * Kl only defines TMP(EAX); without help, spill
				 * would clear EAX but leave EDX in v, failing the
				 * start-block assertion (v == rglob|fn->reg).
				 *
				 * Fix: emit a Kw-class marker copy right after
				 * (which, in backward spill order, is processed
				 * *before* the Kl copy). Its `to = TMP(EDX)`
				 * consumes EDX from v; its `arg[0] = R` makes
				 * emit treat it as a no-op. Forward emit order
				 * keeps the Kl copy first (loads EAX:EDX from
				 * r0's slot) and the marker second (no-op). */
				emit(Ocopy, Kw, TMP(EDX), R, R);
				emit(Ocopy, Kl, TMP(EAX), r0, R);
				ca = 2;
			} else {
				emit(Ocopy, k, TMP(EAX), r0, R);
				ca = 1;
			}
		} else {
			/* i386 cdecl returns float and double in x87 ST(0).  The
			 * emitter understands an Ocopy with destination R as the
			 * “load return value into ST(0)” marker.  No GPR is live. */
			emit(Ocopy, k, R, r0, R);
			ca = 0;
		}
	}

	b->jmp.arg = CALL(ca);
}

static int
argsclass(Ins *i0, Ins *i1, AClass *ac, int op, AClass *aret, int *varc)
{
	Ins *i;
	AClass *a;
	int stk;
	uint n;

	stk = 0;
	*varc = 0;

	if (aret && aret->inmem)
		stk += 4; /* hidden return pointer */

	for (i=i0, a=ac; i<i1; i++, a++)
		switch (i->op - op + Oarg) {
		case Oarg:
			a->size = KWIDE(i->cls) ? 8 : 4;
			a->align = 2;
			a->inmem = 1;
			a->cls[0] = i->cls;
			stk += a->size;
			break;
		case Oargc:
			n = i->arg[0].val;
			typclass(a, &typ[n]);
			stk += a->size;
			break;
		case Oarge:
			/* environment pointer - pass on stack */
			a->size = 4;
			a->align = 2;
			a->inmem = 1;
			stk += a->size;
			break;
		case Oargv:
			*varc = 1;
			a->size = 0;
			a->inmem = 0;
			break;
		default:
			die("unreachable");
		}

	return stk;
}

/* Build a direct call to a named external function.  Used to lower
 * i386 64-bit (Kl) multiply/divide/remainder — which the i386 has no
 * native instructions for — into calls to libc soft-arithmetic
 * helpers (see projects/meuos-libc/src/arch/i386/soft_arith.c).
 * The Ins shape we emit (Oarg/Oarg/Ocall) is identical to a real
 * function call, so selcall lowers it normally. */
static Ref
call_sym(const char *name, Fn *fn)
{
	Con cc;
	memset(&cc, 0, sizeof cc);
	cc.type = CAddr;
	cc.sym.id = intern(name);
	return newcon(&cc, fn);
}

static void
selcall(Fn *fn, Ins *i0, Ins *i1, RAlloc **rap)
{
	Ins *i;
	AClass *ac, *a, aret;
	int ca, stk, off;
	uint n;
	Ref r, r1, r2;
	RAlloc *ra;

	ac = alloc((i1-i0) * sizeof ac[0]);
	aret.inmem = 0;

	if (!req(i1->arg[1], R)) {
		assert(rtype(i1->arg[1]) == RType);
		typclass(&aret, &typ[i1->arg[1].val]);
	}

	stk = argsclass(i0, i1, ac, Oarg, aret.inmem ? &aret : 0, &ca);
	ca = 0;  /* no vararg flag */

	/* round stack to 16-byte alignment */
	stk = (stk + 15) & -16;

	/* stack cleanup (emitted first -> goes to end).
	 * On i386 ILP32 the stack pointer is 4 bytes (Kw). */
	if (stk) {
		emit(Osalloc, Kw, R, getcon(-(int64_t)stk, fn), R);
	}

	if (!req(i1->arg[1], R)) {
		/* struct return */
		if (aret.inmem) {
			/* hidden return pointer is Kw (i386 ILP32). */
			r1 = newtmp("abi", Kw, fn);
			emit(Ocopy, Kw, i1->to, TMP(EAX), R);
			ca += 1;
		}
		/* allocate return pad */
		ra = alloc(sizeof *ra);
		{
			int al = aret.align >= 2 ? aret.align - 2 : 0;
			ra->i = (Ins){Oalloc+al, Kw, r1, {getcon(aret.size, fn)}};
		}
		ra->link = (*rap);
		*rap = ra;
	} else {
		ra = 0;
		if (KBASE(i1->cls) == 0) {
			if (i1->cls == Kl) {
				/* 64-bit return: EDX:EAX pair, denoted by TMP(EAX)
				 * with Kl class (see selret for the convention).
				 * emit's Ocopy Kl decomposes into two movl that
				 * write EAX/EDX to i1->to's slot. dopm recognises
				 * this as a regcpy (arg[0]=TMP(EAX) is a phys reg),
				 * so no separate Ostorel is emitted — the Ocopy
				 * itself writes directly to the slot. */
				emit(Ocopy, Kl, i1->to, TMP(EAX), R);
				ca += 2;
			} else {
				emit(Ocopy, i1->cls, i1->to, TMP(EAX), R);
				ca += 1;
			}
		} else {
		/* The call leaves the x87 result in ST(0).  Store it into the
		 * stack-resident SSA temporary after the call. */
		emit(Ocopy, i1->cls, i1->to, R, R);
		/* Emit a dummy regcpy (arg[0]=TMP(EAX), a physical register)
		 * right after the call so that spill.c's dopm is triggered.
		 * Without a regcpy following the call, dopm is never invoked
		 * and the rsave registers (EAX/ECX/EDX) holding live temps
		 * are not spilled before the call — the call clobbers them
		 * and the values are lost.
		 *
		 * The dummy regcpy has to=R (no destination) and is a no-op
		 * in emit (regcpy emit only moves arg[0] to to, and to=R is
		 * skipped). It exists solely to make regcpy(i) return true
		 * in spill's main loop so dopm handles the preceding Ocall. */
		emit(Ocopy, Kw, R, TMP(EAX), R);
	}
	}

	emit(Ocall, i1->cls, R, i1->arg[0], CALL(ca));

	/* store arguments on stack (right to left = high to low offset).
	 * Stack pointer temp r and arg-address temps r1 are Kw (i386 ILP32). */
	off = 0;
	r = newtmp("abi", Kw, fn);

	/* if struct return, pass hidden pointer first (at offset 0) */
	if (ra && aret.inmem) {
		emit(Ostorew, Kw, R, ra->i.to, r);
		emit(Oadd, Kw, r, TMP(ESP), getcon(off, fn));
		off += 4;
	}

	/* store arguments in order (arg0 at lowest offset) */
	for (i=i0, a=ac; i<i1; i++, a++) {
		if (i->op == Oargv || i->op >= Oarge)
			continue;
		if (i->op == Oargc) {
			/* struct argument: blit to stack */
			r1 = newtmp("abi", Kw, fn);
			emit(Oblit1, 0, R, INT(a->type->size), R);
			emit(Oblit0, 0, R, i->arg[1], r1);
			emit(Oadd, Kw, r1, r, getcon(off, fn));
		} else if (i->cls == Kl) {
			/* Kl argument: i->arg[0] is a Kl value (temp
			 * in slot, or constant). Build a RMem directly
			 * off r (the Osalloc result temp, computed
			 * before this store runs) with offset = off.
			 * emit's Kl Ostorel dispatch stores both low and
			 * high 32 bits.
			 *
			 * We use r (the Osalloc result temp) so that r
			 * stays live and rega/gvn does not dead-code-
			 * eliminate the Osalloc that adjusts ESP by `stk`.
			 * Without Osalloc, the argument stores would land
			 * in main's local area, overwriting the saved EBP
			 * and crashing on `leave; ret` after enough calls.
			 *
			 * rega may place r in EAX, which collides with the
			 * Kl decomposition's use of EAX as source. emit's
			 * Ostorel Kl case detects this collision and uses
			 * ECX instead of EAX for the value register. */
			Mem m = {0};
			m.offset.type = CBits;
			m.offset.bits.i = off;
			m.base = r;
			vgrow(&fn->mem, ++fn->nmem);
			fn->mem[fn->nmem-1] = m;
			emit(Ostorel, Kl, R, i->arg[0], MEM(fn->nmem-1));
		} else {
			/* Scalar float arguments are ordinary cdecl stack bytes, but
			 * they must be written with x87 stores rather than an integer
			 * register-to-memory move. */
			if (KBASE(i->cls) == 1) {
				Mem m = {0};
				m.offset.type = CBits;
				m.offset.bits.i = off;
				m.base = r;
				vgrow(&fn->mem, ++fn->nmem);
				fn->mem[fn->nmem-1] = m;
				emit(i->cls == Kd ? Ostored : Ostores, 0, R,
					i->arg[0], MEM(fn->nmem-1));
			} else {
				r1 = newtmp("abi", Kw, fn);
				emit(Ostorew, Kw, R, i->arg[0], r1);
				emit(Oadd, Kw, r1, r, getcon(off, fn));
			}
		}
		off += a->size;
	}

	/* allocate stack (emitted last -> goes to beginning) */
	emit(Osalloc, Kw, r, getcon(stk, fn), R);
}

static int
selpar(Fn *fn, Ins *i0, Ins *i1)
{
	AClass *ac, *a, aret;
	Ins *i;
	int s, fa;
	Ref r;
	int varc;

	aret.inmem = 0;
	ac = alloc((i1-i0) * sizeof ac[0]);
	curi = &insb[NIns];

	if (fn->retty >= 0)
		typclass(&aret, &typ[fn->retty]);

	fa = argsclass(i0, i1, ac, Opar, aret.inmem ? &aret : 0, &varc);
	fn->reg = i386_sysv_argregs(CALL(0), 0);

	/* handle struct return: first param is hidden pointer.
	 * On i386 ILP32 the hidden return pointer is Kw (4 bytes). */
	if (fn->retty >= 0 && aret.inmem) {
		r = newtmp("abi", Kw, fn);
		emit(Oload, Kw, r, SLOT(-2), R);
		fn->retr = r;
		s = 3;  /* actual params start at slot -3 */
	} else {
		s = 2;  /* first param at slot -2 ([ebp+8]) */
	}

	for (i=i0, a=ac; i<i1; i++, a++) {
		if (i->op == Oargv || i->op == Opare)
			continue;
		if (i->op == Oparc) {
			/* struct parameter: stays on stack */
			fn->tmp[i->to.val].slot = -s;
			s += a->size / 4;
			continue;
		}
		if (i->cls == Kl || KBASE(i->cls) == 1) {
			/* Wide integer and all floating parameters are stack-resident:
			 * incoming bytes are at SLOT(-s). Alias the temporary rather
			 * than emitting a needless load/store through x87. */
			/* Kl parameter: incoming 8 bytes are on the stack at
			 * SLOT(-s). The Kl temp i->to will live in a slot
			 * anyway (kl_in_reg=0), so just alias it to the
			 * incoming slot rather than emitting a load that
			 * would become a slot-to-slot copy.
			 *
			 * But we must keep a defining instruction so that
			 * filluse() (which re-derives tmp[].cls from ins)
			 * sees the right class. Re-emit the Opar Kl as a
			 * no-op marker (arg[0] = R); emit's Kl dispatch
			 * handles Ocopy/Oload/Ostorel but Opar Kl never
			 * reaches emit because T.isel's sel() drops it
			 * via the dead-code fast path (nuse==0 after
			 * aliasing). If the marker survives to emit, it's
			 * a no-op. */
			fn->tmp[i->to.val].slot = -s;
			emit(Opar, i->cls, i->to, R, R);
			s += KWIDE(i->cls) ? 2 : 1;
			continue;
		}
		/* scalar parameter (Kw/Kh/Kb): load from stack slot */
		emit(Oload, i->cls, i->to, SLOT(-s), R);
		s += 1;
	}

	return fa | (s*4)<<12;
}

static Blk *
split(Fn *fn, Blk *b)
{
	Blk *bn;

	++fn->nblk;
	bn = newblk();
	idup(bn, curi, &insb[NIns]-curi);
	curi = &insb[NIns];
	bn->visit = ++b->visit;
	bn->name = strf(PFn, "%s.%d", b->name, b->visit);
	bn->loop = b->loop;
	bn->link = b->link;
	b->link = bn;
	return bn;
}

static void
chpred(Blk *b, Blk *bp, Blk *bp1)
{
	Phi *p;
	uint a;

	for (p=b->phi; p; p=p->link) {
		for (a=0; p->blk[a]!=bp; a++)
			assert(a+1<p->narg);
		p->blk[a] = bp1;
	}
}

static void
selvaarg(Fn *fn, Blk *b, Ins *i)
{
	/* i386 va_list is a simple pointer (Kw, 4 bytes) to the
	 * next vararg on the stack. The IR-level type of i->arg[0]
	 * (the va_list) is Kl on 64-bit targets, but on i386 the
	 * pointer itself is 4 bytes; we model it as Kw here.
	 *
	 * IR sequence (in order, since emit() prepends):
	 *   loc  = load  Kw, ap
	 *   res  = load  cls, loc          (the actual va_arg value)
	 *   new  = add   Kw, loc, step     (step = 4 for Kw, 8 for Kl)
	 *   store Kw, new, ap
	 */
	Ref loc, newloc;
	int step;

	/* i386 va_list is a struct { void *ptr; } passed by reference.
	 * i->arg[0] points to the struct.  Oload from [arguments] gives
	 * struct.ptr (the va_list pointer).  Oload from [loc] gives
	 * the actual vararg.
	 *
	 * Forward order:
	 *   loc  = load Kw, [arguments]   (struct.ptr = va_list pointer)
	 *   res  = load cls, [loc]        (the actual va_arg value)
	 *   new  = add Kw, loc, step      (advance pointer)
	 *   store Kw, new, [arguments]   (update struct field)
	 */
	step = KWIDE(i->cls) ? 8 : 4;
	loc = newtmp("abi", Kw, fn);
	newloc = newtmp("abi", Kw, fn);
	emit(Ostorew, Kw, R, newloc, i->arg[0]);
	emit(Oadd, Kw, newloc, loc, getcon(step, fn));
	if (i->cls == Kl)
		emit(Oload, Kl, i->to, loc, R);
	else
		emit(Oload, i->cls, i->to, loc, R);
	emit(Oload, Kw, loc, i->arg[0], R);
}

static void
selvastart(Fn *fn, int fa, Ref ap)
{
	/* va_list = pointer to first vararg on stack.
	 * The varargs start after all named parameters.
	 *
	 * fa encodes in bits [15:12] the byte offset from
	 * EBP to the first vararg (selpar builds this as
	 * (s*4)<<12 where s starts at 2 to account for the
	 * saved EBP and the return address).
	 *
	 * Use Oadd with EBP instead of Oaddr+SLOT() so the
	 * resulting IR goes through sel()'s case Oadd -> Emit
	 * path. The Oaddr+SLOT() form crashes sel() because
	 * its switch has no case Oaddr (and adding one would
	 * interact badly with fixarg()'s slot-reload logic).
	 * Mirrors amd64's selvastart, but uses Kw (4-byte
	 * pointer) instead of Kl since i386 is ILP32.
	 */
	Ref r0;
	int off;

	off = fa >> 12;
	r0 = newtmp("abi", Kw, fn);
	emit(Ostorew, Kw, R, r0, ap);
	emit(Oadd, Kw, r0, TMP(EBP), getcon(off, fn));
}

void
i386_sysv_abi(Fn *fn)
{
	Blk *b;
	Ins *i, *i0;
	RAlloc *ral;
	int n0, n1, ioff, fa;

	for (b=fn->start; b; b=b->link)
		b->visit = 0;

	/* lower parameters */
	for (b=fn->start, i=b->ins; i<&b->ins[b->nins]; i++)
		if (!ispar(i->op))
			break;
	fa = selpar(fn, b->ins, i);
	n0 = &insb[NIns] - curi;
	ioff = i - b->ins;
	n1 = b->nins - ioff;
	vgrow(&b->ins, n0+n1);
	icpy(b->ins+n0, b->ins+ioff, n1);
	icpy(b->ins, curi, n0);
	b->nins = n0+n1;

	/* lower calls, returns, and vararg instructions */
	ral = 0;
	b = fn->start;
	do {
		if (!(b = b->link))
			b = fn->start; /* do it last */
		if (b->visit)
			continue;
		curi = &insb[NIns];

		/* Pre-pass: i386 has no 64-bit (Kl) mul/div/rem instructions.
		 * Rewrite Omul/Odiv/Orem/Oudiv/Ourem with class Kl into
		 * Oarg/Oarg/Ocall to libc soft-arithmetic helpers, so selcall
		 * lowers them exactly like a normal function call (the Ins
		 * shape is identical to a real call).  Helpers live in
		 * projects/meuos-libc/src/arch/i386/soft_arith.c. */
		{
			int j, nw = 0;
			int max_nins = b->nins + 2 * b->nins + 1;
			Ins *nb = vnew(max_nins, sizeof(Ins), PHeap);
			for (j = 0; j < b->nins; j++) {
				Ins t = b->ins[j];
				if ((t.op == Omul || t.op == Odiv || t.op == Orem
				     || t.op == Oudiv || t.op == Ourem)
				    && t.cls == Kl) {
					Ref rfn;
					const char *sym;
					switch (t.op) {
					case Omul:  sym = "meuos_u64_mul64"; break;
					case Oudiv: sym = "meuos_u64_divu"; break;
					case Ourem: sym = "meuos_u64_remu"; break;
					case Odiv:  sym = "meuos_i64_div"; break;
					case Orem:  sym = "meuos_i64_rem"; break;
					}
					rfn = call_sym(sym, fn);
					nb[nw++] = (Ins){.op=Oarg, .cls=Kl, .to=R, .arg={t.arg[0], R}};
					nb[nw++] = (Ins){.op=Oarg, .cls=Kl, .to=R, .arg={t.arg[1], R}};
					nb[nw++] = (Ins){.op=Ocall, .cls=Kl, .to=t.to, .arg={rfn, R}};
				} else {
					nb[nw++] = t;
				}
			}
			b->ins = nb;
			b->nins = nw;
		}

		selret(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;)
			switch ((--i)->op) {
			default:
				emiti(*i);
				break;
			case Ocall:
				for (i0=i; i0>b->ins; i0--)
					if (!isarg((i0-1)->op))
						break;
				selcall(fn, i0, i, &ral);
				i = i0;
				break;
			case Ovastart:
				selvastart(fn, fa, i->arg[0]);
				break;
			case Ovaarg:
				selvaarg(fn, b, i);
				break;
			case Oarg:
			case Oargc:
				die("unreachable");
			}
		if (b == fn->start)
			for (; ral; ral=ral->link)
				emiti(ral->i);
		idup(b, curi, &insb[NIns]-curi);
	} while (b != fn->start);

	if (debug['A']) {
		fprintf(stderr, "\n> After ABI lowering:\n");
		printfn(fn, stderr);
	}
}
