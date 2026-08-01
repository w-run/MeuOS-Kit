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
	char buf[32];
	Con *c;
	Ref r1;
	int n;

	(void)phi;
	if (rtype(*pr) != RCon)
		return 0;
	c = &fn->con[(*pr).val];
	if (c->type != CBits || KBASE(k) == 0)
		return 0;
	/* VFP has no immediate operands for arithmetic; floating-point
	 * constants are stashed in rodata and loaded with vldr, mirroring
	 * riscv64.  Oload with an RCon source emits `vldr dN, =addr`. */
	n = stashbits(c->bits.i, KWIDE(k) ? 8 : 4);
	vgrow(&fn->con, ++fn->ncon);
	c = &fn->con[fn->ncon-1];
	sprintf(buf, "\"%sfp%d\"", T.asloc, n);
	*c = (Con){.type = CAddr};
	c->sym.id = intern(buf);
	r1 = newtmp("isel", k, fn);
	emit(Oload, k, r1, CON(c-fn->con), R);
	*pr = r1;
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
		Ref a0 = i.arg[0], a1 = i.arg[1];
		Ref t0 = R, t1 = R;
		int pull0 = 0, pull1 = 0;
		/* A constant in the FIRST operand slot cannot be encoded by the
		 * ARM data-processing omap (it writes the source via %0), so
		 * materialize it into a register first.  `div 10, %b` (a
		 * constant dividend, e.g. after partial constant folding) and
		 * `add 5, %x` both reach this.  Ocopy is excluded: its %0 is a
		 * plain move source that loadcon handles directly. */
		if (i.op != Ocopy && rtype(a0) == RCon
		&& KBASE(argcls(&i, 0)) == 0) {
			t0 = newtmp("armc", argcls(&i, 0), fn);
			pull0 = 1;
		}
		/* Unencodable immediate second operand: pull it into a fresh
		 * temp via Ocopy (emitted as movw/movt by loadcon).  ARM
		 * sdiv/udiv have no immediate form at all, so a constant
		 * divisor/remainder must be moved into a register first
		 * ('sdiv r0, r0, #7' is invalid).  CAddr (a symbol/global
		 * address) can likewise never be encoded as a data-processing
		 * immediate.  Floating-point operands never take this path —
		 * VFP has no immediates, and fixarg() below stashes fp
		 * constants in rodata. */
		if (KBASE(argcls(&i, 1)) == 0
		&& uses_imm1(i.op) && rtype(a1) == RCon) {
			c = &fn->con[a1.val];
			if (i.op == Odiv || i.op == Oudiv
			|| i.op == Orem || i.op == Ourem
			|| c->type == CAddr
			|| (c->type == CBits && !arm_imm_ok(c->bits.i))) {
				t1 = newtmp("armc", argcls(&i, 1), fn);
				pull1 = 1;
			}
		}
		if (pull0) i.arg[0] = t0;
		if (pull1) i.arg[1] = t1;
		if (pull0 || pull1) {
			/* Backward pass: emiti() writes in reverse, so emit the
			 * ALU op first (landing last) then the materializations
			 * (landing before it in forward order). */
			emiti(i);
			if (pull1)
				emiti((Ins){.op = Ocopy, .cls = argcls(&i, 1),
				            .to = t1, .arg = {a1, R}});
			if (pull0)
				emiti((Ins){.op = Ocopy, .cls = argcls(&i, 0),
				            .to = t0, .arg = {a0, R}});
			return;
		}
		emiti(i);
		{
			/* fixarg() can emit new instructions and move curi, so
			 * capture the source instruction's operands first. */
			Ref *iarg = curi->arg;
			if (isstore(i.op)) {
				/* Stores bypass the generic argcls() path: ir_ops.h
				 * declares store ops with `T(l,e,e,e, m,e,e,e)` so
				 * for i->cls=Kl the [Kl] column is `e` (Ke), which
				 * makes fixarg take the floating-point immediate
				 * load branch and stash integer store constants in
				 * rodata as floats.  The source class is exactly
				 * i.cls and the address is always Kl. */
				fixarg(&iarg[0], i.cls, 0, fn);
				fixarg(&iarg[1], Kl, 0, fn);
			} else {
				fixarg(&iarg[0], argcls(&i, 0), 0, fn);
				fixarg(&iarg[1], argcls(&i, 1), 0, fn);
			}
		}
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
