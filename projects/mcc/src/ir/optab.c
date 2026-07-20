/* optab.c — IR operator attribute table.
 *
 * Extracted from qbe/parse.c (which mcc no longer compiles — mcc
 * constructs IR directly and never parses text IL). The table itself
 * is referenced by ~18 optimization passes and backends via optab[op]
 * to query canfold/commutes/assoc/idemp/etc. */
#include "ir.h"

/* File-local class enum used by ir_ops.h's T() macro (from reference qbe/parse.c
 * L5-15). Ksb..K0 are signedness-extended variants only needed during
 * text IL parsing; Ke/Km are referenced by optab entries. */
enum {
	Ksb = 4,
	Kub,
	Ksh,
	Kuh,
	Kc,
	K0,

	Ke = -2,
	Km = Kl,
};

Op optab[NOp] = {
#undef F
#define F(cf, hi, id, co, as, im, ic, lg, cv, pn) \
	.canfold = cf, \
	.hasid = hi, .idval = id, \
	.commutes = co, .assoc = as, \
	.idemp = im, \
	.cmpeqwl = ic, .cmplgtewl = lg, .eqval = cv, \
	.pinned = pn
#define O(op, k, flags) [O##op]={.name = #op, .argcls = k, flags},
	#include "ir_ops.h"
#undef F
};
