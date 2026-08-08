/* mdce.c — MIR dead code elimination (MIR_PASS_DCE).
 *
 * Extracted from passes.c during the MIR backend file split (2026-08-07).
 * Iterates to fix-point: removing one dead instruction may leave its
 * operands' defs dead too.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

static uint32_t
mdce_block(MFn *fn, MBlk *b)
{
	(void)fn;
	MIns *out = b->ins;
	uint32_t nout = 0;
	uint32_t removed = 0;

	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		bool has_side_effect = (in->op == MOP_STORE || in->op == MOP_CALL ||
		                        in->op == MOP_ALLOCA || in->op == MOP_VASTART ||
		                        in->op == MOP_VAARG ||
		                        in->op == MOP_SALLOC || in->op == MOP_PAR);
		if (in->dst && in->dst->nuse == 0 && !has_side_effect &&
		    in->dst->kind == MV_TEMP) {
			removed++;
			fn->uses_dirty = true;
			continue;
		}
		out[nout++] = *in;
	}
	b->nins = nout;
	return removed;
}

uint32_t
mdce(MFn *fn)
{
	uint32_t r = 0, round;
	do {
		build_uses(fn);
		round = 0;
		for (MBlk *b = fn->link; b; b = b->link)
			round += mdce_block(fn, b);
		r += round;
	} while (round);
	return r;
}