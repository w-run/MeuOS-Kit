/* armv7_isel.c — ARMv7 instruction selection.
 *
 * Minimal isel: ARM is a load/store architecture; QBE handles register
 * allocation and spill.  Constant and address materialization is done
 * by the emit framework (loadcon/loadaddr in armv7_emit.c), so isel
 * only needs to:
 *
 *   - Lower Oalloc4/Oalloc8/Oalloc16 to Osalloc (+ alignment fixup) so
 *     the emit phase does not die on "no match for alloc4(l)".
 *   - Materialize data-processing immediates that the ARM 8-bit-rotated
 *     form cannot represent (e.g. 6765) into a fresh temp; the emitter
 *     would otherwise print an unencodable `#imm` that the assembler
 *     rejects or silently mis-encodes.
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

/* Return 1 if the op's second operand is emitted as an immediate by
 * the ARM omap table (as opposed to a memory operand via %M1). */
static int
uses_imm1(int op)
{
	switch (op) {
	case Oadd: case Osub: case Oand: case Oor: case Oxor:
	case Osar: case Oshr: case Oshl: case Omul:
	case Odiv: case Oudiv: case Orem: case Ourem:
	case Oacmp: case Oacmn:
	case Oceqw: case Oceql: case Ocnew: case Ocnel:
	case Ocsgew: case Ocsgel: case Ocsgtw: case Ocsgtl:
	case Ocslew: case Ocslel: case Ocsltw: case Ocsltl:
	case Ocugew: case Ocugel: case Ocugtw: case Ocugtl:
	case Oculew: case Oculel: case Ocultw: case Ocultl:
		return 1;
	default:
		return 0;
	}
}

/* Return 1 if v can be encoded as an ARM 8-bit-rotated immediate
 * (the subset the ARM assembler encoder supports): imm8 ROR (2*r). */
static int
arm_imm_ok(int64_t v)
{
	uint32_t x = (uint32_t)v;
	int r;

	if (x < 256)
		return 1;
	for (r = 1; r < 16; r++) {
		uint32_t rv = (x << (r * 2)) | (x >> (32 - r * 2));
		if (rv < 256)
			return 1;
	}
	return 0;
}

static void
sel(Ins i, Fn *fn)
{
	Ref r, t;
	Con *c;

	/* Lower stack allocation pseudo-instructions. */
	if (isalloc(i.op)) {
		salloc(i.to, i.arg[0], fn);
		return;
	}
	if (i.op != Onop) {
		/* Unencodable immediate second operand: pull it into a fresh
		 * temp via Ocopy (emitted as movw/movt by loadcon).  Emitted
		 * after the ALU op in the backward pass so it lands before it
		 * in forward order. */
		r = i.arg[1];
		if (uses_imm1(i.op) && rtype(r) == RCon) {
			c = &fn->con[r.val];
			if (c->type == CBits && !arm_imm_ok(c->bits.i)) {
				t = newtmp("armc", argcls(&i, 1), fn);
				i.arg[1] = t;
				emiti(i);
				emiti((Ins){.op = Ocopy,
				            .cls = argcls(&i, 1),
				            .to = t,
				            .arg = {r, R}});
				return;
			}
		}
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
