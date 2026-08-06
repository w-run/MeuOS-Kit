/* cpp_constexpr_agg.c - m++ (C++) constexpr aggregate-object mini memory model.
 *
 * Stage C.3.3: split from cpp_constexpr.c.  Owns the per-object member-value
 * tables that let a constant-context member access (`*(&s + offset)`) or
 * class-typed return value be folded without a full interpreter pass.
 *
 * Cross-file entry points (all extern in cpp_internal.h):
 *   cpp_cexpr_member_value     (called from eval.c and cpp_constexpr_eval.c)
 *   cpp_record_cexpr_aggregate (called from decl.c and cpp_constexpr_eval.c)
 *   cpp_cexpr_ret_member_value (called from eval.c)
 *   cpp_record_cexpr_return    (called from cpp_constexpr_eval.c)
 *   cpp_copy_cexpr_return      (called from decl.c)
 *   cpp_record_cexpr_member    (called from cpp_constexpr_eval.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

/* --- Mini memory model for constexpr aggregate objects ----------------- */

/* Each initialized member of a constexpr object records
 * (object, byte-offset) -> value so a constant-context member access
 * `*(&s + offset)` can be folded (phase-3 constexpr relaxation).
 * Populated by cpp_record_cexpr_aggregate / cpp_record_cexpr_member. */
struct cexp_obj_member {
	struct decl *obj;
	unsigned long long offset;
	unsigned long long val;
	struct cexp_obj_member *next;
};
static struct cexp_obj_member *g_cexp_obj_members;

/* Look up the stored constant value of `obj`'s member at `offset`, or
 * false when the object/member is not a recorded constexpr value. */
bool
cpp_cexpr_member_value(struct decl *obj, unsigned long long offset,
                       unsigned long long *out)
{
	struct cexp_obj_member *m;
	for (m = g_cexp_obj_members; m; m = m->next)
		if (m->obj == obj && m->offset == offset) {
			*out = m->val;
			return true;
		}
	return false;
}

/* Record the member values of a constexpr aggregate object from its
 * initializer list (`constexpr P p{1, 2}`).  Each init node spans
 * [start, end) bytes of the object and holds one element's expression. */
void
cpp_record_cexpr_aggregate(struct decl *d, struct init *init)
{
	extern struct expr *eval(struct expr *);
	struct init *it;

	if (!d || d->kind != DECLOBJECT || !init)
		return;
	for (it = init; it; it = it->next) {
		struct expr *e;
		if (!it->expr)
			continue;
		e = eval(it->expr);
		if (!e || e->kind != EXPRCONST || !(e->type->prop & PROPINT))
			continue;
		{
			struct cexp_obj_member *m = xmalloc(sizeof *m);
			m->obj = d;
			m->offset = it->start;
			m->val = e->u.constant.u;
			m->next = g_cexp_obj_members;
			g_cexp_obj_members = m;
		}
	}
}

/* Member values of a class object returned by a constexpr function call
 * (`constexpr P make_p(int x) { ... return p; }`, then `make_p(3).a`).
 * Keyed by the call expression node; recorded by cpp_constexpr_eval and
 * consulted by eval()'s member-access folding. */
struct cexp_ret_member {
	struct expr *call;
	unsigned long long offset;
	unsigned long long val;
	struct cexp_ret_member *next;
};
static struct cexp_ret_member *g_cexp_ret_members;

/* Look up a member of a constexpr call's class return value. */
bool
cpp_cexpr_ret_member_value(struct expr *call, unsigned long long offset,
                           unsigned long long *out)
{
	struct cexp_ret_member *m;
	for (m = g_cexp_ret_members; m; m = m->next)
		if (m->call == call && m->offset == offset) {
			*out = m->val;
			return true;
		}
	return false;
}

/* Record the members of `obj` (a class object whose aggregate members were
 * captured) as the return value of `call`. */
void
cpp_record_cexpr_return(struct expr *call, struct decl *obj)
{
	struct cexp_obj_member *m;
	for (m = g_cexp_obj_members; m; m = m->next)
		if (m->obj == obj) {
			struct cexp_ret_member *rm = xmalloc(sizeof *rm);
			rm->call = call;
			rm->offset = m->offset;
			rm->val = m->val;
			rm->next = g_cexp_ret_members;
			g_cexp_ret_members = rm;
		}
}

/* Copy the member values recorded for a constexpr call's class return
 * (`make_p(5)` -> `constexpr P q = make_p(5)`) onto `dst`'s mini-memory
 * entries, so `q.b` can fold.  Returns true when any member was copied. */
bool
cpp_copy_cexpr_return(struct expr *call, struct decl *dst)
{
	struct cexp_ret_member *m;
	bool any = false;
	if (!dst || dst->kind != DECLOBJECT)
		return false;
	for (m = g_cexp_ret_members; m; m = m->next)
		if (m->call == call) {
			struct cexp_obj_member *nm = xmalloc(sizeof *nm);
			nm->obj = dst;
			nm->offset = m->offset;
			nm->val = m->val;
			nm->next = g_cexp_obj_members;
			g_cexp_obj_members = nm;
			any = true;
		}
	return any;
}

/* Record a constant member value into a constexpr aggregate object's mini
 * memory model (so a later member access / return of the object folds it).
 * `obj`/`offset` identify the member; `val` is its constant value. */
void
cpp_record_cexpr_member(struct decl *obj, unsigned long long offset,
                        unsigned long long val)
{
	struct cexp_obj_member *m = xmalloc(sizeof *m);
	m->obj = obj;
	m->offset = offset;
	m->val = val;
	m->next = g_cexp_obj_members;
	g_cexp_obj_members = m;
}