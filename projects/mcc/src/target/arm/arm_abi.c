/* arm_abi.c — ARM AAPCS ABI lowering.
 *
 * AAPCS: 4 GPR (R0-R3) + 8 FPR (D0-D7) arg registers.
 * Return: R0 (int), D0 (float/double).
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
 * supported (matching selcall's lower_args), so it is rejected here. */
static void
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
			 * GPRs (R0:R1, R2:R3, ...) per AAPCS.  The pair-start
			 * register is the Ocopy operand; kl_emit reads the low
			 * half from it and the high half from its successor. */
			int need = (k == Kl) ? 2 : 1;
			if (ngp + need > 4)
				err("arm: %d-th argument past 4 GPR registers is unsupported",
				    (int)(i - i0) + 1);
			emit(Ocopy, k, i->to, TMP(gpreg[ngp]), R);
			ngp += need;
		} else
			err("arm: %d-th argument past 4 GPR/8 FPR registers is unsupported",
			    (int)(i - i0) + 1);
	}
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

/* Update the call's RCall metadata: arg count in bits [4..11] (read by
 * arm32_argregs) and return-value class in bits [0..3] (read by
 * arm32_retregs).  The call's arg[1] is the functype ref from isel, so
 * the return side must be derived from the call result itself —
 * otherwise arm32_retregs reports "no return value", rega never keeps
 * the result register live and the return value is lost (e.g. `f(41)-42`
 * used 41 instead of 42).  Must run BEFORE emiti() so the copied call
 * instruction carries the RCall type. */
static void
call_meta(Ins *call, int nargs, Fn *fn)
{
	int ngp = 0, nfp = 0;
	Ins *argp = call;
	(void)fn;
	for (int j = 0; j < nargs; j++) {
		do { --argp; } while (argp->op == Oargv);
		int k = argp->cls;
		int is_float = KBASE(k) == 1;
		if (is_float && nfp < 8)
			nfp++;
		else if (!is_float) {
			/* Kl args occupy two GPRs (R0:R1, R2:R3, ...) */
			ngp += (k == Kl) ? 2 : 1;
		}
	}
	{
		int rngp = 0, rnfp = 0;
		if (!req(call->to, R)) {
			if (KBASE(call->cls) == 0)
				rngp = (call->cls == Kl) ? 2 : 1;
			else
				rnfp = 1;
		}
		call->arg[1].val = rngp | (rnfp << 2) |
		                   (ngp << 4) | (nfp << 8);
	}
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
 * first pass, then emit back-to-front with those indices. */
static void
emit_call_args(Ins *call, int nargs, Fn *fn)
{
	int gp_idx[64], fp_idx[64];
	int isf[64], acls[64];
	int ngp = 0, nfp = 0;
	Ins *argp = call;

	(void)fn;
	if (nargs > 64)
		err("arm: too many call arguments");
	/* Args precede the call in forward order [arg0..argN-1, call];
	 * `--argp` walks from the call backwards (argN-1 first).  Read the
	 * per-arg class in that order, then assign AAPCS register numbers
	 * in FORWARD order (arg0 gets D0/R0), since the register pools are
	 * indexed by argument position, not by walk order.  Oargv (the
	 * vararg marker) is skipped, matching the nargs count above. */
	for (int j = nargs - 1; j >= 0; j--) {
		do { --argp; } while (argp->op == Oargv);
		acls[j] = argp->cls;
		isf[j] = (KBASE(argp->cls) == 1);
	}
	for (int j = 0; j < nargs; j++) {
		if (isf[j] && nfp < 8) {
			fp_idx[j] = nfp++;
			gp_idx[j] = -1;
		} else if (!isf[j]) {
			/* Kl args occupy two GPRs (R0:R1, R2:R3, ...) */
			int need = (acls[j] == Kl) ? 2 : 1;
			if (ngp + need <= 4) {
				gp_idx[j] = ngp;
				fp_idx[j] = -1;
				ngp += need;
			} else {
				gp_idx[j] = fp_idx[j] = -1; /* stack args unsupported */
			}
		} else {
			gp_idx[j] = fp_idx[j] = -1; /* stack args unsupported */
		}
	}
	argp = call;
	for (int j = nargs - 1; j >= 0; j--) {
		do { --argp; } while (argp->op == Oargv);
		int k = argp->cls;
		int reg = -1;
		if (gp_idx[j] >= 0)
			reg = gpreg[gp_idx[j]];
		else if (fp_idx[j] >= 0)
			reg = fpreg[fp_idx[j]];
		if (reg >= 0)
			emit(Ocopy, k, TMP(reg), argp->arg[0], R);
	}
}

/* arm32_abi — main ABI lowering pass. */
void arm32_abi(Fn *fn) {
	Blk *b; Ins *i, *call_end, *i0;
	int ngp, nfp;
	int n0, n1, ioff;

	/* Lower function parameters into argument registers. */
	b = fn->start;
	curi = &insb[NIns];
	for (i0 = b->ins; i0 < &b->ins[b->nins]; i0++)
		if (!ispar(i0->op))
			break;
	arm32_selpar(fn, b->ins, i0);
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
			call_meta(i, nargs, fn);
			if (!req(i->to, R)) {
				int ck = i->cls;
				Ref rreg = KBASE(ck) == 0 ? TMP(R0) : TMP(D0);
				emit(Ocopy, ck, i->to, rreg, R);
			}
			emiti(*i);
			emit_call_args(i, nargs, fn);
			break;
			}
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
