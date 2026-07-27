/* armv7_abi.c — ARM AAPCS ABI lowering.
 *
 * AAPCS: return in R0 (int/ptr), R0:R1 (64-bit), D0 (float/double).
 *        4 GPR arg registers (R0-R3), 8 VFP arg registers (D0-D7). */
#include "armv7.h"
#include <assert.h>

bits arm32_retregs(Ref r, int p[2]) {
	(void)r;
	if (p) { p[0] = 1; p[1] = 0; }
	return BIT(R0);
}

bits arm32_argregs(Ref r, int p[2]) {
	(void)r;
	if (p) { p[0] = 4; p[1] = 0; }
	return BIT(R0) | BIT(R1) | BIT(R2) | BIT(R3);
}

/* selret — lower block's return jump to Jret0 + copy to return register. */
static void
selret(Blk *b, Fn *fn)
{
	int j, k, cty;
	Ref r;

	j = b->jmp.type;
	(void)fn;

	if (!isret(j) || j == Jret0)
		return;

	r = b->jmp.arg;
	b->jmp.type = Jret0;

	switch (j) {
	case Jretw: case Jretl:
		k = j - Jretw;
		emit(Ocopy, k, TMP(R0), r, R);
		cty = 1;
		break;
	case Jrets: case Jretd:
		k = j - Jretw;
		emit(Ocopy, k, TMP(D0), r, R);
		cty = 1 << 2;
		break;
	default:
		die("unsupported return type");
		cty = 0;
		break;
	}
	b->jmp.arg = CALL(cty);
}

/* arm32_abi — main ABI lowering pass. */
void
arm32_abi(Fn *fn)
{
	Blk *b;

	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];
		selret(b, fn);
		idup(b, curi, &insb[NIns] - curi);
	}
}
