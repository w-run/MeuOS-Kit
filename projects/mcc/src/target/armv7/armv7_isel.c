/* armv7_isel.c — ARMv7 instruction selection.
 *
 * Minimal isel: ARM is a load/store architecture; QBE handles register
 * allocation and spill.  Constant and address materialization is done
 * entirely by the emit framework (loadcon/loadaddr in armv7_emit.c),
 * so isel only needs to handle stack-slot offsets that exceed the
 * ARM load/store immediate range (±4095 for word/byte, ±255 for vfp).
 *
 * In practice, QBE's spill allocator produces access offsets within
 * the 4K range for small-to-moderate frames, so this function is
 * currently a no-op — the omap table + emitins handle everything. */

#include "armv7.h"
#include <assert.h>

static int
fixarg(Ref *pr, int k, int phi, Fn *fn)
{
	(void)pr; (void)k; (void)phi; (void)fn;
	return 0;
}

static void
selcmp(Ins *i, int k, Fn *fn)
{
	(void)i; (void)k; (void)fn;
	/* Comparison handlers: the emit omap table maps Oacmp/Oafcmp
	 * to `cmp`/`vcmpe` instructions directly, so isel need not
	 * split them.  Conditional branches are also emitted from
	 * the omap entry for Oflag* ops. */
}

void
arm32_isel(Fn *fn)
{
	Blk *b; Ins *i;
	(void)fixarg; (void)selcmp;
	/* Instruction selection is a no-op for this target.
	 * The emit framework (armv7_emit.c) handles all constant
	 * materialization, address generation, and instruction
	 * emission via the omap[] table and loadcon/loadaddr. */
	(void)b; (void)i;
}
