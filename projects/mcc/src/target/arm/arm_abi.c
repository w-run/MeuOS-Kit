/* arm_abi.c — ARM AAPCS ABI lowering.
 *
 * AAPCS: 4 GPR (R0-R3) + 8 FPR (D0-D7) arg registers.
 * Return: R0 (int), D0 (float/double).
 */
#include "arm.h"
#include <assert.h>

static int gpreg[4] = {R0, R1, R2, R3};
static int fpreg[8] = {D0, D1, D2, D3, D4, D5, D6, D7};

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

/* Lower call args: emit Ocopy from temp to assigned reg before the call */
static int lower_args(Ins *call, int nargs, Fn *fn) {
	int ngp = 0, nfp = 0, stk = 0;
	Ins *argp = call;
	for (int j = 0; j < nargs; j++) {
		--argp;
		int k = argp->cls;
		int is_float = KBASE(k) == 1;
		int reg = -1;
		if (is_float && nfp < 8) {
			reg = fpreg[nfp++];
		} else if (!is_float && ngp < 4) {
			reg = gpreg[ngp++];
		} else {
			stk += (k == Kl) ? 8 : 4;
		}
		if (reg >= 0) {
			emit(Ocopy, k, TMP(reg), argp->arg[0], R);
		}
	}
	/* Update call metadata */
	call->arg[1].val = (ngp << 4) | (nfp << 8);
	call->arg[1].type = RCall;
	return 0;
}

/* arm32_abi — main ABI lowering pass. */
void arm32_abi(Fn *fn) {
	Blk *b; Ins *i, *call_end;
	int ngp, nfp;

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
					case Oarg: case Oargc: case Oargv:
						nargs++; prev--; continue;
					default: break;
					}
					break;
				}
				lower_args(i, nargs, fn);
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
