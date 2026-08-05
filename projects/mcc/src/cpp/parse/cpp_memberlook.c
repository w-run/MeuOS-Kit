/* cpp_memberlook.c — m++ (C++) member-function lookup helpers.
 *
 * Detect and mangle member-function calls: whether a class (or a base
 * subobject) has a member, count ambiguous inherited members, and resolve
 * the defining class of a method.  cpp_method_member and
 * cpp_member_ambiguous are exported to the postfix/operator lowering.
 *
 * Extracted from cpp_parse.c (split into per-domain submodules).
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

/* --- member function lowering (C.2.3) -------------------------------- */

/* C++ member-function lookup helpers.  A function member is registered in
 * the struct/union member list by addmember (C++ mode); these helpers let
 * the postfix-expression lowering detect and mangle member calls.  The
 * lookup recurses through anonymous members, so inherited members (the
 * base-class subobject is an anonymous member at offset 0) resolve to
 * their defining class. */

/* Does `t` (or any of its base subobjects) contain a member named
 * `name`?  Used to count ambiguous inherited members. */
static bool
cpp_base_contains(struct type *t, const char *name)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name) {
			if (strcmp(m->name, name) == 0)
				return true;
		} else if (m->type && cpp_base_contains(m->type, name)) {
			return true;
		}
	}
	return false;
}

/* Multiple-inheritance member ambiguity: `obj.member` is ambiguous when
 * the name is not a direct member and is defined by more than one base
 * subobject (C++ [class.member.lookup]).  A direct member hides all
 * inherited ones. */
bool
cpp_member_ambiguous(struct type *t, const char *name)
{
	struct member *m;
	int nbases = 0, found = 0;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (m->name && strcmp(m->name, name) == 0)
			return false;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
			++nbases;
			if (cpp_base_contains(m->type, name))
				++found;
		}
	}
	return nbases > 1 && found > 1;
}

/* Find the function member `name` in `t`, optionally reporting the class
 * that defines it (`*owner`).  The class's own members are checked before
 * its base-class subobjects (which are anonymous members), so a derived
 * class's method hides a same-named base method — matching C++ name
 * lookup. */
struct member *
cpp_method_member(struct type *t, const char *name, struct type **owner)
{
	struct member *m, *sub;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name && strcmp(m->name, name) == 0) {
			if (m->type && m->type->kind == TYPEFUNC) {
				if (owner)
					*owner = t;
				return m;
			}
		}
	}
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name) {
			sub = cpp_method_member(m->type, name, owner);
			if (sub)
				return sub;
		}
	}
	return NULL;
}
