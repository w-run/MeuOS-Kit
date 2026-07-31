/* arm_abi.c — ARM AAPCS ABI lowering.
 *
 * AAPCS: 4 GPR (R0-R3) + 8 FPR (D0-D7) arg registers.
 * Return: R0 (int), D0 (float/double).
 *
 * Variadic functions (AAPCS §6.2 / "base standard" marshalling):
 *   - a variadic procedure is marshalled as for the base standard:
 *     there are NO VFP CPRCs, so vararg floats/doubles occupy general
 *     purpose register words (or the stack);
 *   - the first four GPR words go in R0-R3, further words on the stack;
 *   - 4-byte types (int, float, pointer, long in the ILP32 libc) take
 *     one word; 8-byte types (double, long long) take two words and
 *     start at an even word index.
 * mcc models arm pointers/long as 8 bytes (Kl) — indistinguishable in
 * the IR from long long — so Kl arguments are packed as ONE word to
 * match the real AAPCS / the GCC-built libc layout.  The callee's
 * va_arg and the caller's packing use the same word rules, keeping
 * mcc-compiled code self-consistent.
 *
 * The callee spills R0-R3 into a 16-byte register save area at the top
 * of its frame (see arm32_emitfn); the caller just loads R0-R3 with the
 * first four words.  va_start points at word `named_gp` of that area;
 * va_arg walks the area and then the stack arguments, which are
 * contiguous (bl keeps the return address in LR, not on the stack).
 */
#include "arm.h"
#include <assert.h>

static int gpreg[4] = {R0, R1, R2, R3};
static int fpreg[8] = {D0, D1, D2, D3, D4, D5, D6, D7};

/* Lower function parameters into their AAPCS argument registers.
 *
 * Without this binding, rega assigns parameter temps arbitrarily (e.g.
 * R1) while the caller passes them in R0, so the callee reads garbage —
 * observed as `f(41)-42` evaluating to 41-42 (the argument constant
 * reused for the call result).  Beyond the 4 GPR / 8 FPR argument
 * registers, ARM passes scalars on the stack; that path is not yet
 * supported (matching selcall's lower_args), so it is rejected here.
 *
 * Returns the number of GPR words consumed by the named parameters,
 * which is the va_start offset into the register save area.  In a
 * variadic function every parameter word follows the base-standard
 * widths above (Kl = one word), matching the caller's packing. */
static int
arm32_selpar(Fn *fn, Ins *i0, Ins *i1)
{
	Ins *i;
	int ngp = 0, nfp = 0;

	for (i = i0; i < i1; i++) {
		int k = i->cls;
		if (i->op == Oargv || i->op == Opare)
			continue;
		if (i->op == Oparc)
			continue; /* struct params unsupported (call side too) */
		if (KBASE(k) == 1 && nfp < 8)
			emit(Ocopy, k, i->to, TMP(fpreg[nfp++]), R);
		else if (KBASE(k) == 0) {
			/* A 64-bit (Kl) integer parameter occupies two consecutive
			 * GPRs (R0:R1, R2:R3, ...) per AAPCS — except in a
			 * variadic function, where every parameter word follows
			 * the base standard (Kl = one word).  The pair-start
			 * register is the Ocopy operand; kl_emit reads the low
			 * half from it and the high half from its successor. */
			int need = (k == Kl && !fn->vararg) ? 2 : 1;
			if (ngp + need > 4)
				err("arm: %d-th argument past 4 GPR registers is unsupported",
				    (int)(i - i0) + 1);
			emit(Ocopy, k, i->to, TMP(gpreg[ngp]), R);
			ngp += need;
		} else
			err("arm: %d-th argument past 4 GPR/8 FPR registers is unsupported",
			    (int)(i - i0) + 1);
	}
	return ngp;
}

bits arm32_retregs(Ref r, int p[2]) {
	assert(rtype(r) == RCall);
	int ngp = r.val & 3, nfp = (r.val >> 2) & 3;
	if (p) { p[0] = ngp; p[1] = nfp; }
	bits b = 0;
	for (int i = 0; i < ngp; i++) b |= BIT(gpreg[i]);
	for (int i = 0; i < nfp; i++) b |= BIT(fpreg[i]);
	return b;
}

bits arm32_argregs(Ref r, int p[2]) {
	if (rtype(r) != RCall) { if (p) { p[0]=0; p[1]=0; } return 0; }
	int ngp = (r.val >> 4) & 0xF, nfp = (r.val >> 8) & 0xF;
	if (p) { p[0] = ngp; p[1] = nfp; }
	bits b = 0;
	for (int i = 0; i < ngp; i++) b |= BIT(gpreg[i]);
	for (int i = 0; i < nfp; i++) b |= BIT(fpreg[i]);
	return b;
}

/* Lower return: copy value to R0/D0, change Jret to Jret0 */
static void selret(Blk *b, Fn *fn) {
	int j = b->jmp.type;
	(void)fn;
	if (!isret(j) || j == Jret0) return;
	Ref r = b->jmp.arg;
	b->jmp.type = Jret0;
	int cty = 0;
	switch (j) {
	case Jretw: case Jretl:
		emit(Ocopy, j - Jretw, TMP(R0), r, R);
		cty = 1; break;
	case Jrets: case Jretd:
		emit(Ocopy, j - Jretw, TMP(D0), r, R);
		cty = 1 << 2; break;
	default: break;
	}
	b->jmp.arg = CALL(cty);
}

/* Per-argument classification of a call.  `call` points at the Ocall;
 * Oarg/Oargc/Oargv instructions precede it in forward order.  `nargs`
 * is the number of Oarg/Oargc (Oargv markers excluded).
 *
 * For a variadic call (an Oargv marker is present) every argument —
 * named and unnamed — is placed per the base-standard word rules above;
 * otherwise the plain AAPCS rules apply (Kl args take two GPRs, floats
 * go to VFP). */
struct Armcall {
	int va;           /* forward index of the first vararg, or -1 */
	int acls[64];     /* IR class of each Oarg/Oargc */
	int isva[64];     /* true for varargs */
	int gp[64];       /* GPR word index (0..3), or -1 */
	int fp[64];       /* VFP register index (0..7), or -1 */
	int stackoff[64]; /* caller-stack byte offset, or -1 */
	int ngp;          /* GPR words consumed */
	int nfp;          /* VFP registers used (named floats only) */
	int stack;        /* caller-stack bytes */
};

static void
arm32_callclass(Ins *call, int nargs, Ins *lim, struct Armcall *c)
{
	Ins *argp, *first;
	int j, cnt, need, g;

	memset(c, 0, sizeof *c);
	c->va = -1;
	if (nargs > 64)
		err("arm: too many call arguments");

	/* walk back to the first Oarg of this call */
	argp = call;
	cnt = nargs;
	while (cnt > 0) {
		do { --argp; } while (argp->op == Oargv);
		cnt--;
	}
	first = argp;

	/* A leading Oargv (variadic function with no named parameters)
	 * sits immediately before the first Oarg. */
	if (first > lim && first[-1].op == Oargv)
		c->va = 0;

	/* collect classes and the vararg boundary, forward */
	for (j = 0; j < nargs; j++) {
		if (argp->op == Oargv) {
			if (c->va < 0)
				c->va = j;
			argp++;
		}
		c->acls[j] = argp->cls;
		c->isva[j] = c->va >= 0 && j >= c->va;
		argp++;
	}

	/* assign GPR words / VFP registers / caller-stack words.
	 * Named args: ints → GPR words, floats → VFP.  Varargs: all →
	 * GPR words (floats included), words past the fourth go on the
	 * caller stack.  8-byte varargs (Kd) start at an even word so the
	 * libc's 8-byte-aligned va_arg reads line up. */
	g = 0;
	for (j = 0; j < nargs; j++) {
		int k = c->acls[j];
		c->gp[j] = c->fp[j] = -1;
		c->stackoff[j] = -1;
		if (!c->isva[j] && KBASE(k) == 1) {
			/* named float: VFP register */
			if (c->nfp < 8)
				c->fp[j] = c->nfp++;
			else
				err("arm: too many named float arguments");
		} else {
			/* integer (named or vararg) or vararg float.
			 * In a variadic call every word follows the base
			 * standard (Kl counts one word); in a plain call a Kl
			 * argument takes two GPRs as before.  8-byte varargs
			 * (Kd) start at an even word so the libc's 8-byte-aligned
			 * va_arg reads line up. */
			if (c->va >= 0)
				need = (k == Kd) ? 2 : 1;
			else
				need = (k == Kl) ? 2 : 1;
			if (c->va >= 0 && k == Kd && (g & 1))
				g++;	/* even-align 8-byte values */
			if (c->isva[j] && g + need > 4) {
				c->stackoff[j] = c->stack;
				c->stack += 4 * need;
			} else if (g + need > 4) {
				err("arm: %d-th named argument past 4 GPR "
				    "registers is unsupported", j + 1);
			} else {
				c->gp[j] = g;
			}
			g += need;
		}
	}
	c->ngp = g;
}

/* Update the call's RCall metadata: arg count in bits [4..11] (read by
 * arm32_argregs) and return-value class in bits [0..3] (read by
 * arm32_retregs).  The call's arg[1] is the functype ref from isel, so
 * the return side must be derived from the call result itself —
 * otherwise arm32_retregs reports "no return value", rega never keeps
 * the result register live and the return value is lost (e.g. `f(41)-42`
 * used 41 instead of 42).  Must run BEFORE emiti() so the copied call
 * instruction carries the RCall type. */
static void
call_meta(Ins *call, int nargs, Fn *fn, struct Armcall *c)
{
	int rngp = 0, rnfp = 0;
	int ngp = c->ngp < 4 ? c->ngp : 4;

	(void)fn;
	(void)nargs;
	if (!req(call->to, R)) {
		if (KBASE(call->cls) == 0)
			rngp = (call->cls == Kl) ? 2 : 1;
		else
			rnfp = 1;
	}
	call->arg[1].val = rngp | (rnfp << 2) |
	                   (ngp << 4) | (c->nfp << 8);
	call->arg[1].type = RCall;
}

/* Emit Ocopy from temp to assigned register for each call argument.
 * Must run AFTER emiti() of the call: emit() fills *--curi so the
 * last-emitted instruction ends up FIRST in forward order, and the arg
 * copies must precede the call.
 *
 * Args precede the call in forward order [arg0..argN-1, call]; emit()
 * is reverse, so copies are emitted back-to-front.  The AAPCS register
 * assignment is still forward: arg0 gets R0, arg1 gets R1, etc.  To
 * keep both consistent, assign each arg its forward register index in a
 * first pass, then emit back-to-front with those indices.
 *
 * For a variadic call, the caller-stack words (varargs beyond the
 * fourth word) are materialized here too: a stack allocation is created
 * below SP and each stack word is stored into it. */
static void
emit_call_args(Ins *call, int nargs, Fn *fn, struct Armcall *c, Ins *lim)
{
	Ins *argp = call;
	Ref stk;
	int j;

	/* emit the register copies (backwards) */
	for (j = nargs - 1; j >= 0; j--) {
		do { --argp; } while (argp->op == Oargv);
		int k = c->acls[j];
		int reg = -1;
		if (c->gp[j] >= 0)
			reg = gpreg[c->gp[j]];
		else if (c->fp[j] >= 0)
			reg = fpreg[c->fp[j]];
		if (reg < 0)
			continue;
		if (c->gp[j] >= 0 && k == Kd) {
			/* vararg double → GPR pair (vmov rN, rN+1, dM) */
			emit(Ocopy, Kd, TMP(reg), argp->arg[0], R);
		} else if (c->gp[j] >= 0 && k == Ks) {
			/* vararg float → GPR (vmov rN, sM) */
			emit(Ocast, Kw, TMP(reg), argp->arg[0], R);
		} else if (c->gp[j] >= 0 && k == Kl && c->isva[j]) {
			/* pointer/long vararg: only the low word is passed.
			 * (A named Kl in a plain call is a full 64-bit
			 * argument in R0:R1 and falls through to the Ocopy
			 * below.) */
			emit(Ocopy, Kw, TMP(reg), argp->arg[0], R);
		} else {
			emit(Ocopy, k, TMP(reg), argp->arg[0], R);
		}
	}
	if (c->stack == 0)
		return;

	/* caller-stack words: allocate and store.  Forward order at the
	 * call site is [Osalloc, stores, arg copies, call]; emit() is
	 * reverse, so the Osalloc is emitted last (landing first).  The
	 * allocation is rounded up to 8 so sp stays AAPCS-aligned at the
	 * call (the libc's va_arg aligns 8-byte reads to 8). */
	fn->dynalloc = 1;
	stk = newtmp("abi", Kl, fn);

	argp = call;
	for (j = nargs - 1; j >= 0; j--) {
		do { --argp; } while (argp->op == Oargv);
		if (c->stackoff[j] < 0)
			continue;
		int k = c->acls[j];
		Ref addr = newtmp("abi", Kw, fn);
		if (k == Kd) {
			emit(Ostored, Kd, R, argp->arg[0], addr);
		} else if (k == Ks) {
			emit(Ostores, Ks, R, argp->arg[0], addr);
		} else if (k == Kl) {
			Ref lo = newtmp("abi", Kw, fn);
			emit(Ostorew, Kw, R, lo, addr);
			emit(Ocopy, Kw, lo, argp->arg[0], R);
		} else {
			emit(Ostorew, Kw, R, argp->arg[0], addr);
		}
		/* The address computation is emitted after its store so it
		 * lands before it: forward order is [addr = stk+off, store]. */
		emit(Oadd, Kw, addr, stk, getcon(c->stackoff[j], fn));
	}
	emit(Osalloc, Kl, stk, getcon((c->stack + 7) & -8, fn), R);
	(void)lim;
}

/* va_start: __p = r11 + frame + 4*ngp, where frame is the callee frame
 * size (final at emit time) and ngp the GPR words consumed by the named
 * parameters.  The address is encoded as SLOT(-(ngp+1)) so the emitter
 * resolves it once the frame size is known. */
static void
arm32_selvastart(Fn *fn, int ngp, Ref ap)
{
	Ref r0;

	(void)fn;
	r0 = newtmp("abi", Kw, fn);
	emit(Ostorew, Kw, R, r0, ap);		/* *ap = r0 (forward last) */
	emit(Oaddr, Kw, r0, SLOT(-(ngp + 1)), R); /* r0 = r11+frame+4*ngp */
}

/* va_arg: read the next argument through the va_list pointer.
 *
 * The register save area (R0-R3) and the caller-stack words form one
 * contiguous region, so this is a plain pointer walk:
 *   loc = load Kw, [ap]
 *   res = load cls, [loc]
 *   new = loc + step
 *   store Kw, new, [ap]
 *
 * Word rules mirror the caller: 4-byte types step by 4; Kl (pointer/
 * long) reads 4 bytes zero-extended (mcc models it as 8); Kd aligns up
 * to 8 and reads 8. */
static void
arm32_selvaarg(Fn *fn, Ins *i)
{
	Ref loc, newloc;
	int k = i->cls;

	(void)fn;
	loc = newtmp("abi", Kw, fn);
	if (k == Kd) {
		Ref al = newtmp("abi", Kw, fn);
		Ref t7 = newtmp("abi", Kw, fn);
		newloc = newtmp("abi", Kw, fn);
		emit(Ostorew, Kw, R, newloc, i->arg[0]);
		emit(Oadd, Kw, newloc, al, getcon(8, fn));
		emit(Oload, Kd, i->to, al, R);
		emit(Oand, Kw, al, t7, getcon(-8, fn));
		emit(Oadd, Kw, t7, loc, getcon(7, fn));
		emit(Oload, Kw, loc, i->arg[0], R);
	} else if (k == Kl) {
		Ref lo = newtmp("abi", Kw, fn);
		newloc = newtmp("abi", Kw, fn);
		emit(Ostorew, Kw, R, newloc, i->arg[0]);
		emit(Oadd, Kw, newloc, loc, getcon(4, fn));
		emit(Oextuw, Kl, i->to, lo, R);
		emit(Oload, Kw, lo, loc, R);
		emit(Oload, Kw, loc, i->arg[0], R);
	} else {
		int step = KWIDE(k) ? 8 : 4;
		newloc = newtmp("abi", Kw, fn);
		emit(Ostorew, Kw, R, newloc, i->arg[0]);
		emit(Oadd, Kw, newloc, loc, getcon(step, fn));
		emit(Oload, k, i->to, loc, R);
		emit(Oload, Kw, loc, i->arg[0], R);
	}
}

/* arm32_abi — main ABI lowering pass. */
void arm32_abi(Fn *fn) {
	Blk *b; Ins *i, *i0;
	int ngp;
	int n0, n1, ioff;

	/* Lower function parameters into argument registers. */
	b = fn->start;
	curi = &insb[NIns];
	for (i0 = b->ins; i0 < &b->ins[b->nins]; i0++)
		if (!ispar(i0->op))
			break;
	ngp = arm32_selpar(fn, b->ins, i0);
	n0 = &insb[NIns] - curi;
	ioff = i0 - b->ins;
	n1 = b->nins - ioff;
	vgrow(&b->ins, n0+n1);
	icpy(b->ins+n0, b->ins+ioff, n1);
	icpy(b->ins, curi, n0);
	b->nins = n0+n1;

	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];
		selret(b, fn);

		/* Process instructions in reverse. When we see Ocall,
		 * walk forward to count preceding Oarg's, then lower them. */
		for (i = &b->ins[b->nins]; i != b->ins;) {
			--i;
			switch (i->op) {
			case Ocall: {
				/* Count preceding Oarg instructions */
				Ins *prev = i;
				int nargs = 0;
				while (prev > b->ins && prev[-1].op != Onop) {
					switch (prev[-1].op) {
					case Oarg: case Oargc:
						nargs++; prev--; continue;
					case Oargv:
						/* Vararg boundary marker (cls=0, arg={R,R}):
						 * not a real argument, never materialize a
						 * copy for it. */
						prev--; continue;
					default: break;
					}
					break;
				}
			/* emit() fills *--curi (last-emitted ends up FIRST in
			 * forward order after idup), so emit in reverse order:
			 * result copy, then call, then arg copies.  Forward:
			 * arg copies -> call -> result copy.  The RCall metadata
			 * must be written before emiti() so the copied call
			 * carries it. */
			struct Armcall c;
			arm32_callclass(i, nargs, b->ins, &c);
			call_meta(i, nargs, fn, &c);
			if (!req(i->to, R)) {
				int ck = i->cls;
				Ref rreg = KBASE(ck) == 0 ? TMP(R0) : TMP(D0);
				emit(Ocopy, ck, i->to, rreg, R);
			}
			emiti(*i);
			emit_call_args(i, nargs, fn, &c, b->ins);
			break;
			}
			case Ovastart:
				arm32_selvastart(fn, ngp, i->arg[0]);
				break;
			case Ovaarg:
				arm32_selvaarg(fn, i);
				break;
			case Oarg: case Oargc: case Oargv:
			case Opar: case Oparc: case Opare:
				/* Skip these — they're lowered by the args processing */
				break;
			default:
				emiti(*i);
				break;
			}
		}
		idup(b, curi, &insb[NIns] - curi);
	}
}
