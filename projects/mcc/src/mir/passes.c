/* passes.c — MIR optimization pass pipeline (B.2, first batch).
 *
 * Operates on MFn (the platform-neutral MIR) independent of the existing
 * QBE-derived LIR passes.  The per-pass logic has been split into
 * individual files (mfold.c, mloadfwd.c, mdce.c, etc.); this file owns
 * the dispatch, the pipeline, the build_uses infrastructure, and the
 * if-conversion pass.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

/* -dP / --opt-log: when nonzero, run_mir_passes prints a per-pass line for
 * each optimization pass recording how many transformations it performed.
 * Declared in ir.h; the driver sets it for -dP. */
int g_opt_log;

/* -dM (per-pass MIR snapshot): when nonzero, run_plog dumps the full MIR of
 * the function to stderr after each pass.  Declared in ir.h; driver sets it
 * for -dM. */
int g_opt_snapshot;

/* ---- use chain construction ------------------------------------------- */

static void
mark_use(MFn *fn, MVal *v, MIns *in, int argn)
{
	if (!v || v->kind != MV_TEMP)
		return;
	if (v->nuse == v->cuse) {
		v->cuse = v->cuse ? v->cuse * 2 : 4;
		v->use = realloc(v->use, v->cuse * sizeof *v->use);
	}
	v->use[v->nuse].ins = in;
	v->use[v->nuse].phi = 0;
	v->use[v->nuse].argn = argn;
	v->nuse++;
}

static void
build_uses_block(MFn *fn, MBlk *b)
{
	for (uint32_t i = 0; i < b->nins; i++) {
		MIns *in = &b->ins[i];
		if (in->src[0].val)
			mark_use(fn, in->src[0].val, in, 0);
		if (in->src[1].val)
			mark_use(fn, in->src[1].val, in, 1);
	}
	if (b->term.src[0].val)
		mark_use(fn, b->term.src[0].val, &b->term, 0);
	for (MPhi *p = b->phi; p; p = p->link)
		for (uint32_t i = 0; i < p->narg; i++) {
			MVal *v = p->arg[i];
			if (!v || v->kind != MV_TEMP)
				continue;
			if (v->nuse == v->cuse) {
				v->cuse = v->cuse ? v->cuse * 2 : 4;
				v->use = realloc(v->use, v->cuse * sizeof *v->use);
			}
			v->use[v->nuse].ins = 0;
			v->use[v->nuse].phi = p;
			v->use[v->nuse].argn = -1;
			v->nuse++;
		}
}

static void
reset_uses(MFn *fn)
{
	for (uint32_t i = 0; i < fn->nval; i++)
		fn->val[i]->nuse = 0;
}

void
build_uses(MFn *fn)
{
	if (!fn->uses_dirty)
		return;
	fn->uses_dirty = false;
	reset_uses(fn);
	for (MBlk *b = fn->link; b; b = b->link)
		build_uses_block(fn, b);
}

void
build_uses_force(MFn *fn)
{
	fn->uses_dirty = true;
	build_uses(fn);
}

void
mark_uses_dirty(MFn *fn)
{
	fn->uses_dirty = true;
}

/* ---- if-conversion: constant-condition branch simplification (mifconv) ---- */

static uint32_t
mifconv(MFn *fn)
{
	uint32_t r = 0;
	for (MBlk *b = fn->link; b; b = b->link) {
		if (b->term.op != MOP_JNZ)
			continue;
		MRef cond = b->term.src[0];
		if (!cond.con)
			continue;
		if (cond.con->kind == MC_INT) {
			bool taken = cond.con->u.i != 0;
			b->term.op = MOP_JMP;
			b->term.src[0] = (MRef){0};
			if (taken) {
				b->s2 = 0;
			} else {
				b->s1 = b->s2;
				b->s2 = 0;
			}
			r++;
		}
	}
	return r;
}

/* ---- pass pipeline ----------------------------------------------------- */

uint32_t
run_mir_pass(MFn *fn, enum MIRPass pass)
{
	uint32_t n = 0;
	switch (pass) {
	case MIR_PASS_USES:
		build_uses(fn);
		return 0;
	case MIR_PASS_FOLD:
		n = mfold(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_COPY:
		n = mcopy(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_LOADFWD:
		n = mloadfwd(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_MEM2REG:
		n = mmem2reg(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_GVN:
		n = mgvn(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_IFCONV:
		return mifconv(fn);  /* modifies branches, not def/use */
	case MIR_PASS_DCE:
		n = mdce(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_COMBINE:
		n = mcombine(fn);
		if (n) fn->uses_dirty = true;
		return n;
	case MIR_PASS_SSA:
		return mssa_check(fn);  /* readonly */
	default:
		return 0;
	}
}

/* -dP optimization log: human-readable per-pass name. */
static const char *
mir_pass_name(enum MIRPass pass)
{
	switch (pass) {
	case MIR_PASS_USES:    return "uses";
	case MIR_PASS_FOLD:    return "fold";
	case MIR_PASS_COPY:    return "copy";
	case MIR_PASS_LOADFWD: return "loadfwd";
	case MIR_PASS_MEM2REG: return "mem2reg";
	case MIR_PASS_GVN:     return "gvn";
	case MIR_PASS_IFCONV:  return "ifconv";
	case MIR_PASS_DCE:     return "dce";
	case MIR_PASS_COMBINE: return "combine";
	case MIR_PASS_SSA:     return "ssa";
	default:               return "?";
	}
}

static uint32_t
run_plog(MFn *fn, enum MIRPass pass)
{
	uint32_t n = run_mir_pass(fn, pass);
	if (g_opt_snapshot) {
		fprintf(stderr, "\n> MIR after pass %s (%u change%s):\n",
		    mir_pass_name(pass), n, n == 1 ? "" : "s");
		mfn_dump(fn, stderr);
	}
	if (g_opt_log && n)
		fprintf(stderr, "  [%s] %s: %u change%s\n", fn->name,
		    mir_pass_name(pass), n, n == 1 ? "" : "s");
	return n;
}

void
run_mir_passes(MFn *fn, int optlevel)
{
	uint32_t level = (uint32_t)(optlevel < 0 ? 0 : optlevel);
	g_mir_fold_aggressive = (level >= 3 || g_opt_size);
	if (g_opt_log)
		fprintf(stderr, "== opt-log: %s (optlevel %d) ==\n", fn->name, optlevel);

	if (level < 1) {
		build_uses(fn);
		run_plog(fn, MIR_PASS_DCE);
		if (mssa_check(fn))
			fprintf(stderr, "mcc: %s: SSA consistency check FAILED\n",
			        fn->name);
		return;
	}

	run_plog(fn, MIR_PASS_FOLD);
	run_plog(fn, MIR_PASS_IFCONV);
	run_plog(fn, MIR_PASS_COPY);

	if (level >= 2) {
		run_plog(fn, MIR_PASS_MEM2REG);
		run_plog(fn, MIR_PASS_COPY);
		run_plog(fn, MIR_PASS_LOADFWD);
		run_plog(fn, MIR_PASS_GVN);
		run_plog(fn, MIR_PASS_IFCONV);
		run_plog(fn, MIR_PASS_COPY);

		if (level >= 3) {
			run_plog(fn, MIR_PASS_COMBINE);
			msdiv_pow2(fn);
			run_plog(fn, MIR_PASS_FOLD);
			run_plog(fn, MIR_PASS_IFCONV);
			run_plog(fn, MIR_PASS_COPY);
		}
		run_plog(fn, MIR_PASS_DCE);
	}
	if (mssa_check(fn))
		fprintf(stderr, "mcc: %s: SSA consistency check FAILED\n", fn->name);
}