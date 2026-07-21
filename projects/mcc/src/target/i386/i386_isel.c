#include "i386.h"
#include <limits.h>

/* For i386, do the following:
 *
 * - check that constants are used only in
 *   places allowed
 * - ensure immediates always fit in 32b
 * - expose machine register constraints
 *   on instructions like division.
 * - implement fast locals (the streak of
 *   constant allocX in the first basic block)
 * - recognize complex addressing modes
 *
 * Differences from amd64:
 * - 32-bit registers (EAX/ECX/EDX/EBX/ESI/EDI)
 * - No float support (die on Ks/Kd)
 * - Kl arithmetic: add/sub/neg/and/or/xor/load/store/copy
 *   are decomposed in emit (kl_in_reg == 0); mul/div/rem/
 *   shifts still need a soft-arith library (die).
 * - No RIP-relative addressing (absolute)
 * - 8-bit ops limited to EAX/EBX/ECX/EDX
 */

static int amatch(Addr *, Num *, Ref, Fn *);

static int
noimm(Ref r, Fn *fn)
{
	int64_t val;

	if (rtype(r) != RCon)
		return 0;
	switch (fn->con[r.val].type) {
	case CAddr:
		/* i386 uses 32-bit address space, always fits */
		return 0;
	case CBits:
		val = fn->con[r.val].bits.i;
		return (val < INT32_MIN || val > INT32_MAX);
	default:
		die("invalid constant");
	}
}

static int
rslot(Ref r, Fn *fn)
{
	if (rtype(r) != RTmp)
		return -1;
	return fn->tmp[r.val].slot;
}

static int
hascon(Ref r, Con **pc, Fn *fn)
{
	switch (rtype(r)) {
	case RCon:
		*pc = &fn->con[r.val];
		return 1;
	case RMem:
		*pc = &fn->mem[r.val].offset;
		return 1;
	default:
		return 0;
	}
}

/* check if a register has an 8-bit sub-register */
static int
has8bit(int reg)
{
	return reg == EAX || reg == ECX || reg == EDX || reg == EBX;
}

/* ops that require 8-bit register access */
static int
isbyteop(int op)
{
	return op == Ostoreb
		|| op == Oextsb
		|| op == Oextub
		|| (op >= Oflagieq && op <= Oflagiult)
		|| op == Oflagfeq
		|| op == Oflagfne;
}

/* ops that read a byte sub-register from an argument (%B0) */
static int
isbytesrcop(int op)
{
	return op == Ostoreb || op == Oextsb || op == Oextub;
}

static void
fixarg(Ref *r, int k, Ins *i, Fn *fn)
{
	char buf[32];
	Ref r0, r1, r2, r3;
	int s, n, op;
	Con cc, *c;
	Addr a, *m;

	r1 = r0 = *r;
	s = rslot(r0, fn);
	op = i ? i->op : Ocopy;

	/* Store instructions encode their value operand's class as Ke in
	 * optab (the source class always follows i->cls).  Without this,
	 * a Kl store source that lives in a stack slot falls through to the
	 * "slot -> Oaddr" path below and stores the slot *address* instead
	 * of its value.  amd64 never hits this because Kl temps live in
	 * 64-bit registers and are never slot-resident here. */
	if (k == -2)  /* Ke: optab sentinel for store value class */
		k = i->cls;

	if (KBASE(k) == 1 && rtype(r0) == RCon) {
		/* x87 has no immediate floating-point operands.  Materialize the
		 * bit pattern in the literal pool, exactly as the SSE targets do.
		 * All i386 floating temporaries are stack-resident, so the memory
		 * reference is also the natural representation after spill. */
		r1 = MEM(fn->nmem);
		vgrow(&fn->mem, ++fn->nmem);
		memset(&a, 0, sizeof a);
		a.offset.type = CAddr;
		n = stashbits(fn->con[r0.val].bits.i, KWIDE(k) ? 8 : 4);
		sprintf(buf, "\"%sfp%d\"", T.asloc, n);
		a.offset.sym.id = intern(buf);
		fn->mem[fn->nmem-1] = a;
	}
	else if (op == Ocall && r == &i->arg[0]
	&& rtype(r0) == RCon && fn->con[r0.val].type != CAddr) {
		/* use a temporary register so that we
		 * produce an indirect call. On i386 ILP32,
		 * function pointers are Kw (4 bytes). */
		r1 = newtmp("isel", Kw, fn);
		emit(Ocopy, Kw, r1, r0, R);
	}
	else if (op != Ocopy && k == Kl && noimm(r0, fn)) {
		/* load constants that do not fit in
		 * a 32bit signed integer into a
		 * a long temporary */
		r1 = newtmp("isel", Kl, fn);
		emit(Ocopy, Kl, r1, r0, R);
	}
	else if (k == Kl && rtype(r0) == RTmp) {
		/* Kl temps on i386 live in slots (kl_in_reg==0) and
		 * must not be rewritten to their address. Pass them
		 * through unchanged; spill/rega resolve them to their
		 * slot at use, and emit's Kl dispatch reads the right
		 * half directly. */
	}
	else if (s != -1) {
		/* load fast locals' addresses into
		 * temporaries right before the
		 * instruction. On i386 ILP32, addresses are Kw. */
		r1 = newtmp("isel", Kw, fn);
		emit(Oaddr, Kw, r1, SLOT(s), R);
	}
	else if (op != Ocall && hascon(r0, &c, fn)
	&& c->type == CAddr && (c->sym.type & SExt)) {
		/* external symbols: load address via GOT.
		 * On i386 ILP32, addresses are Kw (4 bytes). */
		r1 = newtmp("isel", Kw, fn);
		if (c->bits.i) {
			r2 = newtmp("isel", Kw, fn);
			cc = (Con){.type = CBits};
			cc.bits.i = c->bits.i;
			r3 = newcon(&cc, fn);
			emit(Oadd, Kw, r1, r2, r3);
		} else
			r2 = r1;
		cc = *c;
		cc.bits.i = 0;
		r3 = newcon(&cc, fn);
		emit(Oaddr, Kw, r2, r3, R);
		if (rtype(r0) == RMem) {
			m = &fn->mem[r0.val];
			m->offset.type = CUndef;
			m->base = r1;
			r1 = r0;
		}
	}
	else if (!(isstore(op) && r == &i->arg[1])
	&& !isload(op) && op != Ocall && rtype(r0) == RCon
	&& fn->con[r0.val].type == CAddr) {
		/* turn address operands into
		 * lea/mov instructions. On i386 ILP32, Kw. */
		r1 = newtmp("isel", Kw, fn);
		emit(Oaddr, Kw, r1, r0, R);
	}
	else if (rtype(r0) == RMem) {
		/* eliminate memory operands with
		 * address offsets but no base. On i386 ILP32, Kw. */
		m = &fn->mem[r0.val];
		if (req(m->base, R))
		if (m->offset.type == CAddr) {
			r0 = newtmp("isel", Kw, fn);
			emit(Oaddr, Kw, r0, newcon(&m->offset, fn), R);
			m->offset.type = CUndef;
			m->base = r0;
		}
	}
	else if (isxsel(op) && rtype(*r) == RCon) {
		r1 = newtmp("isel", i->cls, fn);
		emit(Ocopy, i->cls, r1, *r, R);
	}
	*r = r1;
}

static void
seladdr(Ref *r, Num *tn, Fn *fn)
{
	Addr a;
	Ref r0;

	r0 = *r;
	if (rtype(r0) == RTmp) {
		memset(&a, 0, sizeof a);
		if (!amatch(&a, tn, r0, fn))
			return;
		if (!req(a.base, R))
		if (a.offset.type == CAddr) {
			/* When the addressing mode has both a CAddr offset
			 * (typically an external symbol that must be loaded
			 * via GOT) and a base register, move the base to the
			 * index slot. This frees the base position so that
			 * fixarg()'s GOT-loading path can install the GOT
			 * address as the new base without clobbering the
			 * existing index. Mirrors amd64's seladdr handling
			 * (which also covers the apple assembler syntax
			 * restriction there). If we cannot move (index
			 * already in use or base not a tmp), bail out. */
			if (!req(a.index, R) || rtype(a.base) != RTmp)
				return;
			else {
				a.index = a.base;
				a.scale = 1;
				a.base = R;
			}
		}
		chuse(r0, -1, fn);
		vgrow(&fn->mem, ++fn->nmem);
		fn->mem[fn->nmem-1] = a;
		chuse(a.base, +1, fn);
		chuse(a.index, +1, fn);
		*r = MEM(fn->nmem-1);
	}
}

static int
cmpswap(Ref arg[2], int op)
{
	switch (op) {
	case NCmpI+Cflt:
	case NCmpI+Cfle:
		return 1;
	case NCmpI+Cfgt:
	case NCmpI+Cfge:
		return 0;
	}
	return rtype(arg[0]) == RCon;
}

static void
selcmp(Ref arg[2], int k, int swap, Fn *fn)
{
	Ref r;
	Ins *icmp;

	if (swap) {
		r = arg[1];
		arg[1] = arg[0];
		arg[0] = r;
	}
	emit(Oxcmp, k, R, arg[1], arg[0]);
	icmp = curi;
	if (rtype(arg[0]) == RCon) {
		assert(k != Kw);
		icmp->arg[1] = newtmp("isel", k, fn);
		emit(Ocopy, k, icmp->arg[1], arg[0], R);
		fixarg(&curi->arg[0], k, curi, fn);
	}
	fixarg(&icmp->arg[0], k, icmp, fn);
	fixarg(&icmp->arg[1], k, icmp, fn);
}

static void
sel(Ins i, Num *tn, Fn *fn)
{
	Ref r0, r1, tmp[7];
	int x, j, k, kc, sh, swap;
	Ins *i0, *i1;

	if (rtype(i.to) == RTmp)
	if (!isreg(i.to) && !isreg(i.arg[0]) && !isreg(i.arg[1]))
	if (fn->tmp[i.to.val].nuse == 0) {
		chuse(i.arg[0], -1, fn);
		chuse(i.arg[1], -1, fn);
		return;
	}
	i0 = curi;
	k = i.cls;


	switch (i.op) {
	case Odiv:
	case Orem:
	case Oudiv:
	case Ourem:
		if (KBASE(k) == 1)
			goto Emit;
		if (k == Kl)
			die("i386: 64-bit arithmetic not yet supported");
		if (i.op == Odiv || i.op == Oudiv)
			r0 = TMP(EAX), r1 = TMP(EDX);
		else
			r0 = TMP(EDX), r1 = TMP(EAX);
		emit(Ocopy, k, i.to, r0, R);
		emit(Ocopy, k, R, r1, R);
		if (rtype(i.arg[1]) == RCon) {
			/* immediates not allowed for
			 * divisions in x86 */
			r0 = newtmp("isel", k, fn);
		} else
			r0 = i.arg[1];
		if (fn->tmp[r0.val].slot != -1)
			err("unlikely argument %%%s in %s",
				fn->tmp[r0.val].name, optab[i.op].name);
		if (i.op == Odiv || i.op == Orem) {
			emit(Oxidiv, k, R, r0, R);
			emit(Osign, k, TMP(EDX), TMP(EAX), R);
		} else {
			emit(Oxdiv, k, R, r0, R);
			emit(Ocopy, k, TMP(EDX), CON_Z, R);
		}
		emit(Ocopy, k, TMP(EAX), i.arg[0], R);
		fixarg(&curi->arg[0], k, curi, fn);
		if (rtype(i.arg[1]) == RCon)
			emit(Ocopy, k, r0, i.arg[1], R);
		break;
	case Osar:
	case Oshr:
	case Oshl:
		r0 = i.arg[1];
		if (rtype(r0) == RCon)
			goto Emit;
		if (fn->tmp[r0.val].slot != -1)
			err("unlikely argument %%%s in %s",
				fn->tmp[r0.val].name, optab[i.op].name);
		i.arg[1] = TMP(ECX);
		emit(Ocopy, Kw, R, TMP(ECX), R);
		emiti(i);
		i1 = curi;
		emit(Ocopy, Kw, TMP(ECX), r0, R);
		fixarg(&i1->arg[0], argcls(&i, 0), i1, fn);
		break;
	case Onop:
		break;
	case Opar:
		/* Parameter marker emitted by selpar for stack-resident Kl/Ks/Kd
		 * parameters. The temp's slot is already set; no instruction is
		 * needed at isel. Drop it. */
		break;
	case Ostored:
	case Ostores:
	case Ostorel:
	case Ostorew:
	case Ostoreh:
	case Ostoreb:
		/* Note: do NOT collapse Ostorel Kl to Ostorew when the
		 * source is a small constant — that would only store the
		 * low 32 bits. Kl stores always need two movl, handled by
		 * emit's Kl dispatch (the high half is 0 or sign-extended
		 * from the constant). amd64 has an analogous collapse for
		 * Ostored→Ostorel (double→long), but never narrows Kl. */
		seladdr(&i.arg[1], tn, fn);
		goto Emit;
	case_Oload:
		seladdr(&i.arg[0], tn, fn);
		/* Kl loads are decomposed in emit into two movl through EAX
		 * (kl_load_mem_eax).  Pin the address operand away from EAX
		 * so the first movl does not clobber the address before the
		 * high half (or a following Kw load from the same base, e.g.
		 * request->tv_sec then request->tv_nsec) is read.  Covers
		 * both a plain temp address and a memory operand's base. */
		if (i.cls == Kl) {
			Ref base = R;
			if (rtype(i.arg[0]) == RTmp
			&& i.arg[0].val >= Tmp0 && !isreg(i.arg[0]))
				base = i.arg[0];
			else if (rtype(i.arg[0]) == RMem)
				base = fn->mem[i.arg[0].val].base;
			if (rtype(base) == RTmp && base.val >= Tmp0
			&& !isreg(base))
				fn->tmp[base.val].hint.m |= BIT(EAX);
		}
		goto Emit;
	case Odbgloc:
	case Ocall:
	case Osalloc:
	case Ocopy:
	case Oadd:
	case Osub:
	case Oneg:
	case Omul:
	case Oand:
	case Oor:
	case Oxor:
	case Oxtest:
	case Ocast:
	case_Oxsel:
	case_Oext:
	Emit:
		/* On i386, only EAX/EBX/ECX/EDX have 8-bit sub-registers.
		 * The register allocator's hint.m mask is a soft constraint;
		 * when all byte-capable GPRs are busy it falls back to
		 * ESI/EDI/EBP/ESP which have no 8-bit alias.  To make the
		 * constraint hard, pin byte-op sources to EAX (exactly like
		 * shift ops pin the count to ECX) so the allocator inserts
		 * spills as needed.  For byte-op destinations, set the avoid
		 * mask so the allocator prefers byte-capable registers. */
		if (isbytesrcop(i.op) && rtype(i.arg[0]) == RTmp && !isreg(i.arg[0])) {
			r0 = i.arg[0];
			i.arg[0] = TMP(EAX);
			emit(Ocopy, Kw, R, TMP(EAX), R);
			emiti(i);
			i1 = curi;
			fixarg(&i1->arg[0], argcls(&i, 0), i1, fn);
			fixarg(&i1->arg[1], argcls(&i, 1), i1, fn);
			emit(Ocopy, Kw, TMP(EAX), r0, R);
			fixarg(&curi->arg[1], Kw, curi, fn);
		} else if (isbyteop(i.op) && rtype(i.to) == RTmp && !isreg(i.to)) {
			fn->tmp[i.to.val].hint.m |= BIT(ESI)|BIT(EDI)|BIT(EBP)|BIT(ESP);
			emiti(i);
			i1 = curi;
			fixarg(&i1->arg[0], argcls(&i, 0), i1, fn);
			fixarg(&i1->arg[1], argcls(&i, 1), i1, fn);
		} else {
			emiti(i);
			i1 = curi; /* fixarg() can change curi */
			fixarg(&i1->arg[0], argcls(&i, 0), i1, fn);
			fixarg(&i1->arg[1], argcls(&i, 1), i1, fn);
		}
		break;
	case Oalloc4:
	case Oalloc8:
	case Oalloc16:
		salloc(i.to, i.arg[0], fn);
		break;
	case Ouwtof:
	case Oultof:
	case Ostoui:
	case Odtoui:
	case Ostosi:
	case Odtosi:
	case Oswtof:
	case Osltof:
	case Oexts:
	case Otruncd:
		goto Emit;
	default:
		if (isext(i.op))
			goto case_Oext;
		if (isxsel(i.op))
			goto case_Oxsel;
		if (isload(i.op))
			goto case_Oload;
		if (iscmp(i.op, &kc, &x)) {
			swap = cmpswap(i.arg, x);
			if (swap)
				x = cmpop(x);
			emit(Oflag+x, k, i.to, R, R);
			selcmp(i.arg, kc, swap, fn);
			break;
		}
		die("unknown instruction %s", optab[i.op].name);
	}

	while (i0>curi && --i0) {
		/* On i386 with kl_in_reg==0, Kl-class temps legitimately
		 * carry stack slots through isel (e.g. parameters aliased
		 * by selpar, or copies of large constants). Downstream
		 * spill.c / rega.c resolve them to RSlot at use. Allow
		 * slots only when the instruction's class is Kl. */
		if (i0->cls != Kl) {
			assert(rslot(i0->arg[0], fn) == -1);
			assert(rslot(i0->arg[1], fn) == -1);
		}
	}
}

static Ins *
flagi(Ins *i0, Ins *i)
{
	while (i>i0) {
		i--;
		if (i386_op[i->op].zflag)
			return i;
		if (i386_op[i->op].lflag)
			continue;
		return 0;
	}
	return 0;
}

static Ins*
selsel(Fn *fn, Blk *b, Ins *i, Num *tn)
{
	Ref r, cr[2];
	int c, k, swap, gencmp, gencpy;
	Ins *isel0, *isel1, *fi;
	Tmp *t;

	assert(i->op == Osel1);
	for (isel0=i; b->ins<isel0; isel0--) {
		if (isel0->op == Osel0)
			break;
		assert(isel0->op == Osel1);
	}
	assert(isel0->op == Osel0);
	r = isel0->arg[0];
	assert(rtype(r) == RTmp);
	t = &fn->tmp[r.val];
	fi = flagi(b->ins, isel0);
	cr[0] = cr[1] = R;
	gencmp = gencpy = swap = 0;
	k = Kw;
	c = Cine;
	if (!fi || !req(fi->to, r)) {
		gencmp = 1;
		cr[0] = r;
		cr[1] = CON_Z;
	}
	else if (iscmp(fi->op, &k, &c)) {
		swap = cmpswap(fi->arg, c);
		if (swap)
			c = cmpop(c);
		if (t->nuse == 1) {
			gencmp = 1;
			cr[0] = fi->arg[0];
			cr[1] = fi->arg[1];
			*fi = (Ins){.op = Onop};
		}
	}
	else if (fi->op == Oand && t->nuse == 1
	     && (rtype(fi->arg[0]) == RTmp ||
	         rtype(fi->arg[1]) == RTmp)) {
		fi->op = Oxtest;
		fi->to = R;
		if (rtype(fi->arg[1]) == RCon) {
			r = fi->arg[1];
			fi->arg[1] = fi->arg[0];
			fi->arg[0] = r;
		}
	}
	else {
		/* since flags are not tracked in liveness,
		 * the result of the flag-setting instruction
		 * has to be marked as live
		 */
		if (t->nuse == 1)
			gencpy = 1;
	}
	/* generate conditional moves */
	for (isel1=i; isel0<isel1; --isel1) {
		isel1->op = Oxsel+c;
		sel(*isel1, tn, fn);
	}
	assert(!gencmp || !gencpy);
	if (gencmp)
		selcmp(cr, k, swap, fn);
	if (gencpy)
		emit(Ocopy, Kw, R, r, R);
	*isel0 = (Ins){.op = Onop};
	return isel0;
}

static void
seljmp(Blk *b, Fn *fn)
{
	Ref r;
	int c, k, swap;
	Ins *fi;
	Tmp *t;

	if (b->jmp.type == Jret0
	|| b->jmp.type == Jjmp
	|| b->jmp.type == Jhlt)
		return;
	assert(b->jmp.type == Jjnz);
	r = b->jmp.arg;
	b->jmp.arg = R;
	/* Constant-folded conditions can reach target lowering as RCon. */
	if (rtype(r) == RCon) {
		int nonzero = fn->con[r.val].type == CBits && fn->con[r.val].bits.i != 0;
		b->jmp.type = Jjmp;
		b->s1 = nonzero ? b->s1 : b->s2;
		b->s2 = 0;
		return;
	}
	t = &fn->tmp[r.val];
	assert(rtype(r) == RTmp);
	if (b->s1 == b->s2) {
		chuse(r, -1, fn);
		b->jmp.type = Jjmp;
		b->s2 = 0;
		return;
	}
	fi = flagi(b->ins, &b->ins[b->nins]);
	if (!fi || !req(fi->to, r)) {
		selcmp((Ref[2]){r, CON_Z}, Kw, 0, fn);
		b->jmp.type = Jjf + Cine;
	}
	else if (iscmp(fi->op, &k, &c)) {
		swap = cmpswap(fi->arg, c);
		if (swap)
			c = cmpop(c);
		if (t->nuse == 1) {
			selcmp(fi->arg, k, swap, fn);
			*fi = (Ins){.op = Onop};
		}
		b->jmp.type = Jjf + c;
	}
	else if (fi->op == Oand && t->nuse == 1
	     && (rtype(fi->arg[0]) == RTmp ||
	         rtype(fi->arg[1]) == RTmp)) {
		fi->op = Oxtest;
		fi->to = R;
		b->jmp.type = Jjf + Cine;
		if (rtype(fi->arg[1]) == RCon) {
			r = fi->arg[1];
			fi->arg[1] = fi->arg[0];
			fi->arg[0] = r;
		}
	}
	else {
		/* since flags are not tracked in liveness,
		 * the result of the flag-setting instruction
		 * has to be marked as live
		 */
		if (t->nuse == 1)
			emit(Ocopy, Kw, R, r, R);
		b->jmp.type = Jjf + Cine;
	}
}

enum {
	Pob,
	Pbis,
	Pois,
	Pobis,
	Pbi1,
	Pobi1,
};

/* mgen generated code (same as amd64 - x86 addressing modes) */

static int
opn(int op, int l, int r)
{
	static uchar Oaddtbl[91] = {
		2,
		2,2,
		4,4,5,
		6,6,8,8,
		4,4,9,10,9,
		7,7,5,8,9,5,
		4,4,12,10,12,12,12,
		4,4,9,10,9,9,12,9,
		11,11,5,8,9,5,12,9,5,
		7,7,5,8,9,5,12,9,5,5,
		11,11,5,8,9,5,12,9,5,5,5,
		4,4,9,10,9,9,12,9,9,9,9,9,
		7,7,5,8,9,5,12,9,5,5,5,9,5,
	};
	int t;

	if (l < r)
		t = l, l = r, r = t;
	switch (op) {
	case Omul:
		if (2 <= l)
		if (r == 0) {
			return 3;
		}
		return 2;
	case Oadd:
		return Oaddtbl[(l + l*l)/2 + r];
	default:
		return 2;
	}
}

static int
refn(Ref r, Num *tn, Con *con)
{
	int64_t n;

	switch (rtype(r)) {
	case RTmp:
		if (!tn[r.val].n)
			tn[r.val].n = 2;
		return tn[r.val].n;
	case RCon:
		if (con[r.val].type != CBits)
			return 1;
		n = con[r.val].bits.i;
		if (n == 8 || n == 4 || n == 2 || n == 1)
			return 0;
		return 1;
	default:
		return INT_MIN;
	}
}

static bits match[13] = {
	[4] = BIT(Pob),
	[5] = BIT(Pbi1),
	[6] = BIT(Pob) | BIT(Pois),
	[7] = BIT(Pob) | BIT(Pobi1),
	[8] = BIT(Pbi1) | BIT(Pbis),
	[9] = BIT(Pbi1) | BIT(Pobi1),
	[10] = BIT(Pbi1) | BIT(Pbis) | BIT(Pobi1) | BIT(Pobis),
	[11] = BIT(Pob) | BIT(Pobi1) | BIT(Pobis),
	[12] = BIT(Pbi1) | BIT(Pobi1) | BIT(Pobis),
};

static uchar *matcher[] = {
	[Pbi1] = (uchar[]){
		1,3,1,3,2,0
	},
	[Pbis] = (uchar[]){
		5,1,8,5,27,1,5,1,2,5,13,3,1,1,3,3,3,2,0,1,
		3,3,3,2,3,1,0,1,29
	},
	[Pob] = (uchar[]){
		1,3,0,3,1,0
	},
	[Pobi1] = (uchar[]){
		5,3,9,9,10,33,12,35,45,1,5,3,11,9,7,9,4,9,
		17,1,3,0,3,1,3,2,0,3,1,1,3,0,34,1,37,1,5,2,
		5,7,2,7,8,37,29,1,3,0,1,32
	},
	[Pobis] = (uchar[]){
		5,2,10,7,11,19,49,1,1,3,3,3,2,1,3,0,3,1,0,
		1,3,0,5,1,8,5,25,1,5,1,2,5,13,3,1,1,3,3,3,
		2,0,1,3,3,3,2,26,1,51,1,5,1,6,5,9,1,3,0,51,
		3,1,1,3,0,45
	},
	[Pois] = (uchar[]){
		1,3,0,1,3,3,3,2,0
	},
};

/* end of generated code */

static void
anumber(Num *tn, Blk *b, Con *con)
{
	Ins *i;
	Num *n;

	for (i=b->ins; i<&b->ins[b->nins]; i++) {
		if (rtype(i->to) != RTmp)
			continue;
		n = &tn[i->to.val];
		n->l = i->arg[0];
		n->r = i->arg[1];
		n->nl = refn(n->l, tn, con);
		n->nr = refn(n->r, tn, con);
		n->n = opn(i->op, n->nl, n->nr);
	}
}

static Ref
adisp(Con *c, Num *tn, Ref r, Fn *fn, int s)
{
	Ref v[2];
	int n;

	while (!req(r, R)) {
		assert(rtype(r) == RTmp);
		n = refn(r, tn, fn->con);
		if (!(match[n] & BIT(Pob)))
			break;
		runmatch(matcher[Pob], tn, r, v);
		assert(rtype(v[0]) == RCon);
		addcon(c, &fn->con[v[0].val], s);
		r = v[1];
	}
	return r;
}

static int
amatch(Addr *a, Num *tn, Ref r, Fn *fn)
{
	static int pat[] = {Pobis, Pobi1, Pbis, Pois, Pbi1, -1};
	Ref ro, rb, ri, rs, v[4];
	Con *c, co;
	int s, n, *p;

	if (rtype(r) != RTmp)
		return 0;

	n = refn(r, tn, fn->con);
	memset(v, 0, sizeof v);
	for (p=pat; *p>=0; p++)
		if (match[n] & BIT(*p)) {
			runmatch(matcher[*p], tn, r, v);
			break;
		}
	if (*p < 0)
		v[1] = r;

	memset(&co, 0, sizeof co);
	ro = v[0];
	rb = adisp(&co, tn, v[1], fn, 1);
	ri = v[2];
	rs = v[3];
	s = 1;

	if (*p < 0 && co.type != CUndef)
	if (amatch(a, tn, rb, fn))
		return addcon(&a->offset, &co, 1);
	if (!req(ro, R)) {
		assert(rtype(ro) == RCon);
		c = &fn->con[ro.val];
		if (!addcon(&co, c, 1))
			return 0;
	}
	if (!req(rs, R)) {
		assert(rtype(rs) == RCon);
		c = &fn->con[rs.val];
		assert(c->type == CBits);
		s = c->bits.i;
	}
	ri = adisp(&co, tn, ri, fn, s);
	*a = (Addr){co, rb, ri, s};

	if (rtype(ri) == RTmp)
	if (fn->tmp[ri.val].slot != -1) {
		if (a->scale != 1
		|| fn->tmp[rb.val].slot != -1)
			return 0;
		a->base = ri;
		a->index = rb;
	}
	if (!req(a->base, R)) {
		assert(rtype(a->base) == RTmp);
		s = fn->tmp[a->base.val].slot;
		if (s != -1)
			a->base = SLOT(s);
	}
	return 1;
}

/* instruction selection
 * requires use counts (as given by parsing)
 */
void
i386_isel(Fn *fn)
{
	Blk *b, **sb;
	Ins *i;
	Phi *p;
	uint a;
	int n, al;
	int64_t sz;
	Num *num;

	/* assign slots to fast allocs */
	b = fn->start;
	/* specific to NAlign == 3 */
	for (al=Oalloc, n=4; al<=Oalloc1; al++, n*=2)
		for (i=b->ins; i<&b->ins[b->nins]; i++)
			if (i->op == al) {
				if (rtype(i->arg[0]) != RCon)
					break;
				sz = fn->con[i->arg[0].val].bits.i;
				if (sz < 0 || sz >= INT_MAX-15)
					err("invalid alloc size %"PRId64, sz);
				sz = (sz + n-1) & -n;
				sz /= 4;
				if (sz > INT_MAX - fn->slot)
					die("alloc too large (raw=%" PRId64 " slot=%d n=%d)\n",
					    fn->con[i->arg[0].val].bits.i, fn->slot, n);
				fn->tmp[i->to.val].slot = fn->slot;
				fn->slot += sz;
				fn->salign = 2 + al - Oalloc;
				*i = (Ins){.op = Onop};
			}

	/* process basic blocks */
	n = fn->ntmp;
	num = emalloc(n * sizeof num[0]);
	for (b=fn->start; b; b=b->link) {
		curi = &insb[NIns];
		for (sb=(Blk*[3]){b->s1, b->s2, 0}; *sb; sb++)
			for (p=(*sb)->phi; p; p=p->link) {
				for (a=0; p->blk[a] != b; a++)
					assert(a+1 < p->narg);
				fixarg(&p->arg[a], p->cls, 0, fn);
			}
		memset(num, 0, n * sizeof num[0]);
		anumber(num, b, fn->con);
		seljmp(b, fn);
		for (i=&b->ins[b->nins]; i!=b->ins;) {
			--i;
			assert(i->op != Osel0);
			if (i->op == Osel1)
				i = selsel(fn, b, i, num);
			else
				sel(*i, num, fn);
		}
		idup(b, curi, &insb[NIns]-curi);
	}
	free(num);

	if (debug['I']) {
		fprintf(stderr, "\n> After instruction selection:\n");
		printfn(fn, stderr);
	}
}
