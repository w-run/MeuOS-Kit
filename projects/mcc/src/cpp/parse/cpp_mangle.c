/* cpp_mangle.c — m++ (C++) name mangling for overload resolution.
 *
 * C++-style type codes appended to a base name so that overloads with
 * different signatures get distinct symbols (``Class_method_ii``,
 * ``free_fn_ii``).  References the owner-class lookup via
 * cpp_method_member; cpp_mangle_type is shared by the vtable /
 * requires / member-template code in cpp_parse.c.
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

/* Append the mangling code for one type into buf (NUL-terminated). */
void
cpp_mangle_type(struct type *t, char *buf, size_t bufsz)
{
	char *p = buf;
	char *end = buf + bufsz - 1;

	if (!t) {
		*p++ = 'v';
		goto out;
	}
	/* C++ references mangle with a distinct marker so `f(Vec)`, `f(Vec &)`
	 * and `f(Vec &&)` get different overload names (the caller binds an
	 * object by address, but the types must not collide): 'R' = lvalue
	 * reference, 'V' = rvalue reference (move overloads). */
	if (t->isref && t->kind == TYPEPOINTER) {
		if (p + 1 <= end)
			*p++ = t->isrref ? 'V' : 'R';
		t = t->base;
	}
	switch (t->kind) {
	case TYPEVOID:     *p++ = 'v'; break;
	case TYPEBOOL:     *p++ = 'b'; break;
	case TYPECHAR:     *p++ = t->u.arith.issigned ? 'c' : 'C'; break;
	case TYPECHAR8:    *p++ = 'D'; break;   /* Itanium ABI for char8_t */
	case TYPESHORT:    *p++ = t->u.arith.issigned ? 's' : 'S'; break;
	case TYPEINT:      *p++ = t->u.arith.issigned ? 'i' : 'u'; break;
	case TYPELONG:     *p++ = t->u.arith.issigned ? 'l' : 'L'; break;
	case TYPELLONG:    *p++ = t->u.arith.issigned ? 'j' : 'J'; break;
	case TYPEFLOAT:    *p++ = 'f'; break;
	case TYPEDOUBLE:   *p++ = 'd'; break;
	case TYPELDOUBLE:  *p++ = 'e'; break;
	case TYPEPOINTER:  *p++ = 'p'; break;
	case TYPEENUM:     *p++ = 'E'; break;
	case TYPEARRAY:    *p++ = 'A'; break;
	case TYPENULLPTR:  *p++ = 'n'; break;
	case TYPESTRUCT:
	case TYPEUNION:
		*p++ = 'o';
		if (t->u.structunion.tag) {
			size_t n = strlen(t->u.structunion.tag);
			if (p + n <= end) {
				memcpy(p, t->u.structunion.tag, n);
				p += n;
			}
		}
		break;
	default:           *p++ = 'x'; break;
	}
out:
	*p = '\0';
}

/* Mangled name of method `name` of class `t`, with the given argument
 * expressions' types appended for overload resolution:
 * `Class_method_ii` etc.  Returns the name in buf.
 *
 * `prefer_ref` marks lvalue arguments as bindable by reference ('R'
 * prefix), matching C++'s preference for reference overloads on
 * lvalues; the caller falls back to the plain (by-value) encoding when
 * no reference overload exists. */
void
cpp_mangled_name_args(struct type *t, const char *name, struct expr *args,
                      char *buf, size_t bufsz, bool prefer_ref)
{
	struct type *owner = NULL;
	size_t n;

	if (cpp_method_member(t, name, &owner) && owner)
		t = owner;
	snprintf(buf, bufsz, "%s_%s",
	         (t && t->u.structunion.tag) ? t->u.structunion.tag : "anon",
	         name);
	n = strlen(buf);
	for (; args; args = args->next) {
		char code[64];
		size_t cl;
		/* overload-resolution value category: lvalue args prefer the
		 * lvalue-reference overload ('R'), rvalue (temporary) args prefer
		 * the rvalue-reference/move overload ('V') */
		if (prefer_ref && n + 1 < bufsz)
			buf[n++] = args->lvalue ? 'R' : 'V';
		cpp_mangle_type(args->type, code, sizeof code);
		cl = strlen(code);
		if (n + cl < bufsz) {
			memcpy(buf + n, code, cl + 1);
			n += cl;
		}
	}
}

/* --- free-function overloading (file/namespace scope) --------------- */

/* Mangled name of a file-scope (free) function `name` whose function
 * type is `funct`, with the parameter types appended exactly like the
 * member scheme (`helper_ii` for `int helper(int, int)`).  Used at
 * declaration time: a same-name free function with a different
 * signature is registered under this name instead of being rejected as
 * a conflicting redeclaration. */
void
cpp_free_mangle_name(const char *name, struct type *funct, char *buf,
                     size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s_", name);
	n = strlen(buf);
	if (funct && funct->kind == TYPEFUNC) {
		for (cur = funct->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Mangled name of a free-function call `name(args...)`, encoded from
 * the argument expression types for overload resolution (`helper_ii`).
 * `prefer_ref` marks the value category of each argument — lvalue with
 * 'R', rvalue (temporary) with 'V' — so `f(Vec&)` wins on lvalues and
 * `f(Vec&&)` (the move overload) wins on temporaries, falling back to
 * the by-value overload when no reference variant exists. */
void
cpp_free_mangle_name_args(const char *name, struct expr *args, char *buf,
                          size_t bufsz, bool prefer_ref)
{
	size_t n;

	snprintf(buf, bufsz, "%s_", name);
	n = strlen(buf);
	for (; args; args = args->next) {
		char code[64];
		size_t cl;
		if (prefer_ref && n + 1 < bufsz)
			buf[n++] = args->lvalue ? 'R' : 'V';
		cpp_mangle_type(args->type, code, sizeof code);
		cl = strlen(code);
		if (n + cl < bufsz) {
			memcpy(buf + n, code, cl + 1);
			n += cl;
		}
	}
}

const char *
cpp_mangled_name(struct type *t, const char *name, char *buf, size_t bufsz)
{
	struct type *owner = NULL;

	/* inherited methods mangle under the defining base class, so
	 * `d.base_method()` resolves to `Base_base_method` */
	if (cpp_method_member(t, name, &owner) && owner)
		t = owner;
	snprintf(buf, bufsz, "%s_%s",
	         (t && t->u.structunion.tag) ? t->u.structunion.tag : "anon",
	         name);
	return buf;
}
