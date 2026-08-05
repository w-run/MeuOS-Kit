/* passes.c — LIR optimization + codegen pipeline driver.
 *
 * run_passes() is the single entry point that runs the full backend
 * pipeline on a LIR Fn: ABI lowering (T.abi0) -> CFG -> SSA -> fold/GVN/
 * GCM/coalesce -> T.abi1 -> isel -> liveness -> spill -> rega -> postra.
 *
 * Historically this lived in src/irgen/emit.c (the C frontend's IR
 * construction file); it is moved here into the shared backend (libmcc.a)
 * so the MIR → LIR bridge (src/lir/bridge.c) and future frontends can run
 * the same pipeline without depending on the C frontend.
 */
#include <assert.h>

#include "ir.h"

void
run_passes(Fn *fn)
{
	uint n;
	int ol = fn->optlevel;
#define P(name) name(fn)

	P(T.abi0);
	P(fillcfg);
	P(filluse);
	if (ol >= 1) P(promote);
	P(filluse);
	P(ssa);
	P(filluse);
	P(ssacheck);
	P(fillalias);
	if (ol >= 1) P(loadopt);
	P(filluse);
	P(fillalias);
	if (ol >= 1) P(coalesce);
	P(filluse);
	P(filldom);
	P(ssacheck);
	if (ol >= 1) P(gvn);
	P(fillcfg);
	if (ol >= 1) P(simplcfg);
	P(filluse);
	P(filldom);
	if (ol >= 2) P(gcm);
	P(filluse);
	P(ssacheck);
	if (T.cansel && ol >= 2) {
		P(ifconvert);
		P(fillcfg);
		P(filluse);
		P(filldom);
		P(ssacheck);
	}
	P(T.abi1);
	if (ol >= 1) P(simpl);
	P(fillcfg);
	P(filluse);
	P(T.isel);
	P(fillcfg);
	P(filllive);
	P(fillloop);
	P(fillcost);
	P(spill);
	P(rega);
	if (ol >= 1) P(postra);
	P(fillcfg);
	/* P(slotmerge); -- disabled (defect J): slotmerge miscompiles large
	 * functions, breaking self-host (self-built mcc SIGSEGVs on any input).
	 * See .issues/0802.md defect J.  Re-enable after root cause fixed. */
	P(simpljmp);
	P(fillcfg);
	/* link blocks in rpo order, terminating the last with link=0 */
	assert(fn->rpo[0] == fn->start);
	for (n=0;; n++)
		if (n == fn->nblk-1) {
			fn->rpo[n]->link = 0;
			break;
		} else
			fn->rpo[n]->link = fn->rpo[n+1];
#undef P
}
