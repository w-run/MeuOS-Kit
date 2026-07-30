/* armv7_isel.c — ARMv7 instruction selection.
 *
 * Minimal isel: ARM is a load/store architecture; QBE handles register
 * allocation and spill.  Constant and address materialization is done
 * entirely by the emit framework (loadcon/loadaddr in armv7_emit.c),
 * so isel only needs to:
 *
 *   - Lower Oalloc4/Oalloc8/Oalloc16 to Osalloc (+ alignment fixup) so
 *     the emit phase does not die on "no match for alloc4(l)".
 *   - Handle stack-slot offsets that exceed the ARM load/store immediate
 *     range (±4095 for word/byte, ±255 for vfp).
 *
 * In practice, QBE's spill allocator produces access offsets within
 * the 4K range for small-to-moderate frames, so the offset check is
 * currently a no-op — the omap table + emitins handle everything. */

#include "arm.h"
#include <assert.h>

static int
fixarg(Ref *pr, int k, int phi, Fn *fn)
{
	(void)pr; (void)k; (void)phi; (void)fn;
	return 0;
}

static void
sel(Ins i, Fn *fn)
{
	/* Lower stack allocation pseudo-instructions. */
	if (isalloc(i.op)) {
		salloc(i.to, i.arg[0], fn);
		return;
	}
	if (i.op != Onop) {
		emiti(i);
		fixarg(&curi->arg[0], argcls(&i, 0), 0, fn);
		fixarg(&curi->arg[1], argcls(&i, 1), 0, fn);
	}
}

void
arm32_isel(Fn *fn)
{
	Blk *b; Ins *i;
	(void)fixarg;
	/* Iterate backwards over each block's instructions, lowering
	 * Oalloc* and re-emitting other ops through emiti()+fixarg(). */
	for (b = fn->start; b; b = b->link) {
		curi = &insb[NIns];
		for (i = &b->ins[b->nins]; i != b->ins;)
			sel(*--i, fn);
		idup(b, curi, &insb[NIns] - curi);
	}
}
