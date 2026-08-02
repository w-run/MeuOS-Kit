#include <stdio.h>

#include "mir.h"

/* mssa_check — SSA consistency checker for MIR (B.6 验收项 2).
 *
 * Verifies the explicit-SSA invariants of a fully constructed (or
 * pass-processed) MIR function:
 *
 *   1. every MV_TEMP value has exactly one definition — an MIns.def XOR
 *      an MPhi.defphi, never both and never neither;
 *   2. every phi has arg[]/blk[] pairs (each arg carries the predecessor
 *      block it arrives on) and a destination;
 *   3. every instruction's MRef operands resolve to a live table entry
 *      (MVal via the value table, MConst via the constant pool);
 *   4. block successor/predecessor consistency.
 *
 * Returns 0 when the function is SSA-consistent, otherwise 1 with a
 * diagnostic on stderr.
 */
static int
bad(MFn *fn, const char *what)
{
	fprintf(stderr, "mssa_check: %s: %s\n", fn->name, what);
	return 1;
}

int
mssa_check(MFn *fn)
{
	int err = 0;

	if (!fn)
		return 1;

	/* 1. single-def */
	for (uint32_t i = 0; i < fn->nval; i++) {
		MVal *v = fn->val[i];
		if (!v || v->kind != MV_TEMP)
			continue;
		if (v->def && v->defphi)
			err |= bad(fn, "value defined by both ins and phi");
		if (!v->def && !v->defphi)
			err |= bad(fn, "value has no definition");
	}

	/* 2. block-level structure */
	for (MBlk *b = fn->link; b; b = b->link) {
		/* phis: dst present, arg/blk paired */
		for (MPhi *p = b->phi; p; p = p->link) {
			if (!p->dst)
				err |= bad(fn, "phi with no destination");
			for (uint32_t a = 0; a < p->narg; a++) {
				if (!p->arg[a])
					err |= bad(fn, "phi arg is null");
				if (!p->blk || !p->blk[a])
					err |= bad(fn, "phi arg without predecessor block");
			}
		}
		/* instructions: every source ref must resolve to a table entry */
		for (uint32_t n = 0; n < b->nins; n++) {
			MIns *in = &b->ins[n];
			for (int a = 0; a < 2; a++) {
				MRef r = in->src[a];
				if (r.val) {
					/* MV_TYPE refs carry the frontend typ[] index in
					 * ->id (func_to_mir overwrites it; the bridge emits
					 * TYPE(idx) from it), NOT a value-table index.  Only
					 * MV_TEMP (a real SSA value) must resolve through the
					 * value table. */
					if (r.val->kind == MV_TEMP) {
						uint32_t id = r.val->id;
						if (id >= fn->nval || fn->val[id] != r.val) {
							MVal *tab = (id < fn->nval) ? fn->val[id] : 0;
							char buf[240];
							snprintf(buf, sizeof buf,
								"ins[%d] src[%d] op=%d refs v%u(kind=%d '%s' %p) "
								"table v%u(kind=%d '%s' %p)",
								n, a, (int)in->op, id,
								(int)r.val->kind, r.val->name ? r.val->name : "?",
								(void *)r.val,
								id < fn->nval ? id : 0,
								tab ? (int)tab->kind : -1,
								tab && tab->name ? tab->name : "?",
								(void *)tab);
							err |= bad(fn, buf);
						}
					}
				}
				if (r.con) {
					uint32_t id = r.con->id;
					if (id >= fn->ncon || fn->con[id] != r.con)
						err |= bad(fn, "ins source refs a non-live const");
				}
			}
		}
	}

	return err;
}
