/* cpp_expr_op.c — m++ (C++) C++ operator-overload / access-control
 * lowering in expressions.
 *
 * Lowers operator/template-membership calls on class objects to the
 * overloaded operator method (cpp_member_op_call), manages access-control
 * (cpp_same_class_context / cpp_member_accessible), and builds temporary
 * object construction (cpp_temp_construct).  Entry points are exported;
 * cpp_member_op_call is module-internal.
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

/* Operator-overload mangling code for a punctuation token: `operator+`
 * lowers to the method name `operator_pl`, mangled `Class_operator_pl`.
 * Returns NULL for operators without a user-overloadable spelling. */
const char *
cpp_op_mangle(enum tokenkind op)
{
	switch (op) {
	case TADD:     return "pl";
	case TSUB:     return "mi";
	case TMUL:     return "ml";
	case TDIV:     return "dv";
	case TMOD:     return "rm";
	case TEQL:     return "eq";
	case TNEQ:     return "ne";
	case TLESS:    return "lt";
	case TLEQ:     return "le";
	case TGREATER: return "gt";
	case TGEQ:     return "ge";
	case TINC:     return "pp";
	case TDEC:     return "mm";
	case TBAND:    return "ad";
	case TBOR:     return "or";
	case TXOR:     return "er";
	case TLNOT:    return "nt";
	case TLPAREN:  return "cl";   /* operator() — functors / lambdas */
	case TLBRACK:  return "ix";   /* operator[] — subscript (C++23 P2128) */
	case TSPACESHIP: return "ss"; /* operator<=> — three-way comparison */
	default:       return NULL;
	}
}

/* Define a static data member out-of-line: `int Class::count = 0;`.  The
 * declarator already mangled the name to `Class_count`; find the in-class
 * declaration and emit its storage. */
void
cpp_define_static_data(struct scope *s, const char *qclass, const char *name)
{
	extern struct scope filescope;
	extern struct init *parseinit(struct scope *, struct type *);
	extern void emitdata(struct decl *, struct init *);

	struct type *ct;
	struct decl *d;
	struct init *init = NULL;

	ct = scopegettag(s, qclass, true);
	if (!ct || (ct->kind != TYPESTRUCT && ct->kind != TYPEUNION))
		error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", qclass);
	d = scopegetdecl(ct->scope ? ct->scope : &filescope, name, 1);
	if (!d || d->kind != DECLOBJECT) {
		error_code(E_DECL, &tok.loc, "no static data member '%s' in class '%s'",
		      name, qclass);
		return;
	}
	if (tok.kind == TASSIGN) {
		next();
		init = parseinit(s, d->type);
	} else if (!d->defined && !d->tentative) {
		/* `int Class::count;` without initializer is a tentative
		 * definition; defer to the normal sweep. */
		d->tentative = true;
	}
	if (tok.kind == TSEMICOLON)
		next();
	if (init || !d->tentative) {
		emitdata(d, init);
		d->defined = true;
	}
}

/* Build a member operator call `l.operator_<mname>(r)` when the class
 * `t` has that operator.  const-K × reference-R 级联查找：声明侧形如
 * `Vec_operator_eqKRoVec`（const 成员函数追加 K，引用形参前缀 R）。调用
 * 侧按实参编码，缺 K/R 会查不到。依次尝试 4 种变体：对象 const 匹配的 K
 * 态优先，引用编码（prefer_ref 给 lvalue 加 'R'、rvalue 加 'V'）与裸编码
 * 各试一轮，命中即用。  Returns true and sets *out on success. */
static bool
cpp_member_op_call(struct type *t, const char *mname, struct expr *l,
                   struct expr *r, struct expr **out)
{
	extern struct scope filescope;

	char mangled[256];
	struct decl *fd;
	struct expr *fn, *obj, *call, **end;
	bool obj_const = (l->qual & QUALCONST) != 0;
	bool found = false;
	int kk;

	for (kk = 0; kk < 2 && !found; kk++) {
		/* kk=0 先试与对象 const 性匹配的 K 态，kk=1 回退另一态 */
		const char *ks = (kk == 0) == obj_const ? "K" : "";
		char mnameQ[64];
		int rref;
		snprintf(mnameQ, sizeof mnameQ, "%s%s", mname, ks);
		for (rref = 0; rref < 2 && !found; rref++) {
			cpp_mangled_name_args(t, mnameQ, r, mangled,
			    sizeof mangled, rref != 0);
			fd = scopegetdecl(t->scope ? t->scope : &filescope,
			    mangled, 1);
			found = fd && fd->kind == DECLFUNC;
		}
	}
	if (!found)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_operator_pl */

	obj = mkunaryexpr(TBAND, l); /* &l */
	obj->type = mkpointertype(t, l->qual);

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	end = &obj->next;
	if (r) {
		struct decl *pp = fd->type->u.func.params ?
		    fd->type->u.func.params->next : NULL;
		struct expr *arg = r;
		/* C++ reference parameter: bind the address
		 * (expr_postfix.c:352-354 惯例) */
		if (pp && pp->type && pp->type->isref)
			arg = mkunaryexpr(TBAND, r);
		*end = exprassign(arg, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	*out = call;
	return true;
}

/* Lower `l op r` to a member operator call `l.operator_pl(r)` when the
 * left operand is a class type with that operator overloaded.  Falls
 * back to the C++20 rewritten candidates ([over.match.oper]/3.4): with
 * no direct `operator<`/`==`/..., `a op b` rewrites to `(a <=> b) op 0`
 * using the class's `operator<=>`.  Returns true and sets *out on
 * success (caller keeps normal arithmetic). */
bool
cpp_try_operator_call(struct scope *s, struct expr *l, enum tokenkind op,
                      struct expr *r, struct expr **out)
{
	extern struct scope filescope;

	struct type *t = l ? l->type : NULL;
	const char *opcode;
	char mname[64];
	struct decl *fd;
	struct expr *fn, *call, **end;

	opcode = cpp_op_mangle(op);
	if (!opcode || !t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	snprintf(mname, sizeof mname, "operator_%s", opcode);
	if (cpp_is_member_function(t, mname))
		return cpp_member_op_call(t, mname, l, r, out);
	/* C++20 rewritten candidates: with no direct member operator,
	 * `a < b` / `a == b` / ... rewrite to `(a <=> b) < 0` / `(a <=> b)
	 * == 0` / ... via the class's `operator<=>`. */
	if ((op == TLESS || op == TLEQ || op == TGREATER || op == TGEQ ||
	    op == TEQL || op == TNEQ) &&
	    cpp_is_member_function(t, "operator_ss")) {
		struct expr *ss;
		if (cpp_member_op_call(t, "operator_ss", l, r, &ss) &&
		    ss->type && (ss->type->prop & PROPREAL)) {
			*out = mkbinaryexpr(&tok.loc, op, ss,
			    mkconstexpr(&typeint, 0));
			return true;
		}
	}
	/* non-member operator overload: `operator_pl(a, b)` registered as a
	 * free function in the current scope */
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return false;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &operator_pl */

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	{
		struct decl *pp = fd->type->u.func.params;
		struct expr *arg = l;
		if (pp && pp->type && pp->type->isref)
			arg = mkunaryexpr(TBAND, l);
		*end = exprassign(arg, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
		pp = pp ? pp->next : NULL;
		if (r) {
			arg = r;
			if (pp && pp->type && pp->type->isref)
				arg = mkunaryexpr(TBAND, r);
			*end = exprassign(arg, pp ? pp->type : NULL);
			end = &(*end)->next;
			++call->u.call.nargs;
		}
	}
	*out = call;
	return true;
}

/* Lower `obj[args...]` to a member operator[] call
 * `obj.operator_ix(args...)`.  The subscript is a postfix operator, so
 * unlike cpp_try_operator_call (binary `l op r`) the object is always the
 * first argument and the bracket contents are a comma-separated argument
 * list — C++23 P2128 allows operator[] to take any number of parameters.
 * Returns true and sets *out on success (caller keeps the builtin
 * subscript error). */
bool
cpp_subscript_call(struct scope *s, struct expr *obj, struct expr *args,
                   struct expr **out)
{
	extern struct scope filescope;

	struct type *t = obj ? obj->type : NULL;
	const char *mname = "operator_ix";
	char mangled[512];
	struct decl *fd;
	struct expr *fn, *o, *call, **end;
	struct decl *param;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	/* member operator[] first.  A const member mangles with a K right
	 * after the method name (`Vec_operator_ixKi`).  Resolve the overload
	 * from the object's constness like the ordinary member-call path
	 * (cpp_parse.c this_decl const): a const object must pick the const
	 * (K) overload, a non-const object prefers the non-const one and
	 * falls back to a const method. */
	if (cpp_is_member_function(t, mname)) {
		bool obj_const = (obj->qual & QUALCONST) != 0;
		char mnameQ[64];

		snprintf(mnameQ, sizeof mnameQ, "%s%s", mname,
		    obj_const ? "K" : "");
		cpp_mangled_name_args(t, mnameQ, args, mangled, sizeof mangled, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, mangled, 1);
		if (!fd || fd->kind != DECLFUNC) {
			snprintf(mnameQ, sizeof mnameQ, "%s%s", mname,
			    obj_const ? "" : "K");
			cpp_mangled_name_args(t, mnameQ, args, mangled, sizeof mangled, false);
			fd = scopegetdecl(t->scope ? t->scope : &filescope, mangled, 1);
		}
		if (fd && fd->kind == DECLFUNC) {
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_operator_ix */

			o = mkunaryexpr(TBAND, obj); /* &obj */
			o->type = mkpointertype(t, obj->qual);

			call = mkexpr(EXPRCALL, fd->type->base, fn);
			call->u.call.args = o;
			call->u.call.nargs = 1;
			end = &o->next;
			/* mtype param[0] is the implicit `this`; explicit params
			 * follow. */
			param = fd->type->u.func.params ? fd->type->u.func.params->next
			                                : NULL;
			for (; args; args = args->next, param = param ? param->next : NULL) {
				*end = exprassign(args, param ? param->type : NULL);
				end = &(*end)->next;
				++call->u.call.nargs;
			}
			*out = call;
			return true;
		}
	}
	/* C++23 P1169 static operator[]: `static int& operator[](Matrix& m,
	 * int i, int j)`.  The object is an explicit first parameter and the
	 * member has no implicit `this`, so the mangled name is
	 * `Class_operator_ix<objparam><args>S` — the object-parameter type
	 * (reference 'R'/'V' or by-value before the class code) is encoded
	 * first, then the bracket args, then the static-member "S" suffix.
	 * The encodings are tried in value-category order (an lvalue object
	 * most likely binds a `T&` object parameter). */
	{
		const char *ord[3];
		int oi;
		struct type *owner = NULL;
		const char *tag;

		if (cpp_method_member(t, mname, &owner) && owner)
			tag = owner->u.structunion.tag;
		else
			tag = t->u.structunion.tag;
		if (!tag)
			tag = "anon";
		ord[0] = (obj && obj->lvalue) ? "R" : "V";
		ord[1] = "";
		ord[2] = (obj && obj->lvalue) ? "V" : "R";
		for (oi = 0; oi < 3; oi++) {
			char argcodes[256];
			char *p = argcodes;
			char *e = argcodes + sizeof argcodes - 1;
			struct expr *a;

			/* the bracket args encode once with plain param types (a
			 * static declaration has no R/V lvalue prefixes) */
			for (a = args; a && p < e; a = a->next) {
				char code[64];
				size_t cl;
				cpp_mangle_type(a->type, code, sizeof code);
				cl = strlen(code);
				if (p + cl >= e)
					break;
				memcpy(p, code, cl);
				p += cl;
			}
			*p = '\0';
			snprintf(mangled, sizeof mangled, "%s_%s%so%s%sS",
			    tag, mname, ord[oi], tag, argcodes);
			fd = scopegetdecl(t->scope ? t->scope : &filescope,
			    mangled, 1);
			if (!fd || fd->kind != DECLFUNC)
				continue;
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_operator_ix...S */

			call = mkexpr(EXPRCALL, fd->type->base, fn);
			call->u.call.args = NULL;
			call->u.call.nargs = 0;
			end = &call->u.call.args;
			/* fd's first parameter is the explicit object parameter
			 * (no implicit `this`); bind the object by address when it
			 * is a reference, like every other reference param */
			param = fd->type->u.func.params;
			o = obj;
			if (param && param->type && param->type->isref)
				o = mkunaryexpr(TBAND, obj);
			*end = exprassign(o, param ? param->type : NULL);
			end = &(*end)->next;
			++call->u.call.nargs;
			param = param ? param->next : NULL;
			for (; args; args = args->next,
			    param = param ? param->next : NULL) {
				struct expr *arg = args;
				if (param && param->type && param->type->isref)
					arg = mkunaryexpr(TBAND, args);
				*end = exprassign(arg, param ? param->type : NULL);
				end = &(*end)->next;
				++call->u.call.nargs;
			}
			*out = call;
			return true;
		}
	}
	/* non-member operator[]: `operator_ix(obj, args...)` registered as a
	 * free function (C++23 P2128R8 allows non-member subscripts) */
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return false;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	param = fd->type->u.func.params;
	*end = exprassign(obj, param ? param->type : NULL);
	end = &(*end)->next;
	++call->u.call.nargs;
	param = param ? param->next : NULL;
	for (; args; args = args->next, param = param ? param->next : NULL) {
		*end = exprassign(args, param ? param->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	*out = call;
	return true;
}

/* Construct a temporary class object: `Vec(expr)` lowers to allocating an
 * anonymous temporary, running the constructor, and yielding the
 * temporary as an lvalue expression.  Returns the expression (the
 * temporary's value), or NULL if the class has no matching constructor. */
struct expr *
cpp_temp_construct(struct scope *s, struct type *ct)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);

	struct expr *args = NULL, **ae = &args;
	struct expr *e;
	struct decl *tmp;

	if (!ct || (ct->kind != TYPESTRUCT && ct->kind != TYPEUNION))
		return NULL;

	next(); /* consume '(' */
	while (tok.kind != TRPAREN) {
		if (args)
			expect(TCOMMA, "or ')' after constructor argument");
		*ae = assignexpr(s);
		ae = &(*ae)->next;
	}
	next(); /* consume ')' */

	if (!curfunc)
		return NULL;

	/* anonymous temporary */
	tmp = mkdecl("tmp", DECLOBJECT, ct, QUALNONE, LINKNONE);
	tmp->u.obj.storage = SDAUTO;
	funcinit(curfunc, tmp, NULL, false); /* allocate storage */
	cpp_emit_ctor_call(curfunc, tmp, args);

	e = mkexpr(EXPRIDENT, ct, NULL);
	e->qual = QUALNONE;
	/* anonymous temporary is an rvalue: the value-category marker drives
	 * overload resolution so a temporary prefers the move/rvalue overload
	 * over the copy/lvalue one.  IR generation addresses it via its decl
	 * regardless of this flag. */
	e->lvalue = false;
	e->u.ident.decl = tmp;
	return e;
}

/* Is `t` the class whose method body is currently being parsed?  Inside a
 * method body, bare member names resolve (cpp_member_ident) and direct
 * member access is allowed regardless of access level. */
bool
cpp_same_class_context(struct type *t)
{
	if (!g_cpp_method.active || !g_cpp_method.class_type)
		return false;
	return g_cpp_method.class_type == t;
}

/* Enforce C++ access control on `obj.member` / `obj->member` access:
 * private members are only reachable from within the member's own class;
 * protected members additionally from derived classes (friend and
 * virtual inheritance are later stages).  Returns true when the access
 * is allowed. */
bool
cpp_member_accessible(struct type *t, struct member *m)
{
	struct cpp_friend *fr;

	if (!m || m->access == ACC_PUBLIC)
		return true;
	if (m->access == ACC_PROTECTED && cpp_is_derived(g_cpp_method.class_type, t))
		return true;
	if (cpp_same_class_context(t))
		return true;
	/* friend classes of `t` (recorded by `friend class B;` in its body)
	 * may access its private/protected members from their own methods;
	 * friend free functions are covered by cpp_friend_decl pointing the
	 * method context at the befriending class while their body is parsed */
	if (g_cpp_method.active && g_cpp_method.class_type)
		for (fr = t->u.structunion.friends; fr; fr = fr->next)
			if (fr->cls == g_cpp_method.class_type)
				return true;
	return false;
}
