/* mloadfwd.c — MIR store→load forwarding + dead-store elimination.
 *
 * Extracted from passes.c during the MIR backend file split (2026-08-07).
 * The frontend lowers every scalar into its variable slot (store) and
 * reloads it (load); store→load pairs for non-escaping local slots are
 * redundant.  Forwarding replaces the load with a copy of the stored value
 * (COPY propagates it), and dead-store elimination drops stores whose slot
 * is never read.
 */
#include <stdlib.h>
#include <string.h>

#include "mir.h"

typedef struct MLoadMap {
	MVal *addr;          /* slot address value */
	MVal *val;           /* last stored value */
	struct MLoadMap *next;
} MLoadMap;

static MLoadMap *
mloadfwd_get(MLoadMap *m, MVal *addr)
{
	for (; m; m = m->next)
		if (m->addr == addr)
			return m;
	return 0;
}

static bool
mloadfwd_slot(MFn *fn, const bool *is_alloca, MVal *v)
{
	if (!v || v->kind != MV_TEMP)
		return false;
	if (!is_alloca[v->id])
		return false;
	for (uint32_t i = 0; i < v->nuse; i++) {
		MUse *u = &v->use[i];
		if (u->phi || !u->ins)
			return false;
		MIns *in = u->ins;
		bool base = (in->op == MOP_LOAD && u->argn == 0) ||
		            (in->op == MOP_STORE && u->argn == 1);
		if (!base)
			return false;
	}
	return true;
}

uint32_t
mloadfwd(MFn *fn)
{
	uint32_t r = 0;
	bool *is_alloca = calloc(fn->nval ? fn->nval : 1, sizeof *is_alloca);
	for (MBlk *b = fn->link; b; b = b->link)
		for (uint32_t i = 0; i < b->nins; i++)
			if (b->ins[i].op == MOP_ALLOCA && b->ins[i].dst)
				is_alloca[b->ins[i].dst->id] = true;

	for (MBlk *b = fn->link; b; b = b->link) {
		build_uses(fn);
		MLoadMap *map = 0;
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op == MOP_STORE) {
				MVal *addr = in->src[1].val;
				if (!mloadfwd_slot(fn, is_alloca, addr))
					continue;
				MLoadMap *e = mloadfwd_get(map, addr);
				if (!e) {
					e = calloc(1, sizeof *e);
					e->addr = addr;
					e->next = map;
					map = e;
				}
				e->val = in->src[0].val;
			} else if (in->op == MOP_LOAD) {
				MVal *addr = in->src[0].val;
				if (!mloadfwd_slot(fn, is_alloca, addr))
					continue;
				MLoadMap *e = mloadfwd_get(map, addr);
				if (e && e->val) {
					in->op = MOP_COPY;
					in->src[0].val = e->val;
					in->src[0].con = 0;
					in->src[1].val = 0;
					in->src[1].con = 0;
					r++;
				}
			}
		}
		while (map) {
			MLoadMap *nx = map->next;
			free(map);
			map = nx;
		}
	}

	build_uses(fn);
	for (MBlk *b = fn->link; b; b = b->link) {
		bool *drop = calloc(b->nins ? b->nins : 1, sizeof *drop);
		for (uint32_t i = 0; i < b->nins; i++) {
			MIns *in = &b->ins[i];
			if (in->op != MOP_STORE)
				continue;
			MVal *addr = in->src[1].val;
			if (!mloadfwd_slot(fn, is_alloca, addr))
				continue;
			bool only_stores = true;
			for (uint32_t j = 0; j < addr->nuse; j++) {
				MUse *u = &addr->use[j];
				if (!u->ins || u->ins->op != MOP_STORE ||
				    u->argn != 1) {
					only_stores = false;
					break;
				}
			}
			if (only_stores) {
				drop[i] = true;
				r++;
			}
		}
		MIns *out = b->ins;
		uint32_t nout = 0;
		for (uint32_t i = 0; i < b->nins; i++)
			if (!drop[i])
				out[nout++] = b->ins[i];
		b->nins = nout;
		free(drop);
	}
	free(is_alloca);
	build_uses(fn);
	return r;
}