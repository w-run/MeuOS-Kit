/* arm_abi.c — ARM AAPCS ABI lowering.
 *
 * AAPCS (ARM Procedure Call Standard) for armv7 with VFPv3-D16:
 *   - Integer/pointer args in r0-r3 (4 GPRs). Stack beyond.
 *   - Float/double args in d0-d7 (8 FPRs). Stack beyond.
 *   - Return: r0 (32-bit), r0:r1 (64-bit), d0 (float/double).
 *   - HFA (Homogeneous FP Aggregate): up to 4 members in d0-d3.
 */
#include "arm.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

enum {
	Cptr  = 1,
	Cstk1 = 2,
	Cstk2 = 4,
	Cstk  = Cstk1 | Cstk2,
	Cfpint = 8,
};

typedef struct Class Class;
struct Class {
	char class;
	int cls[2];
	int reg[2];
	int off[2];
	char ngp, nfp, nreg;
};

static int gpreg[4] = {R0, R1, R2, R3};
static int fpreg[8] = {D0, D1, D2, D3, D4, D5, D6, D7};

/* RCall layout: [0:1]=gp_ret, [2:3]=fp_ret, [4:7]=gp_arg, [8:11]=fp_arg */
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
	if (rtype(r) != RCall) {
		if (p) { p[0] = 0; p[1] = 0; }
		return 0;
	}
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
		cty = 1;
		break;
	case Jrets: case Jretd:
		emit(Ocopy, j - Jretw, TMP(D0), r, R);
		cty = 1 << 2;
		break;
	default: break;
	}
	b->jmp.arg = CALL(cty);
}

/* arm32_abi — main ABI lowering pass. */
void arm32_abi(Fn *fn) {
	Blk *b; Ins *i;
	int ngp, nfp, stk;
	Class c;

	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];
		selret(b, fn);

		for (i = &b->ins[b->nins]; i != b->ins;) {
			--i;
			switch (i->op) {
			case Oarg: case Oargc: case Oargv:
			case Opar: case Oparc: case Opare:
				emiti(*i);
				break;
			default:
				emiti(*i);
				break;
			}
		}
		idup(b, curi, &insb[NIns] - curi);
	}
}
