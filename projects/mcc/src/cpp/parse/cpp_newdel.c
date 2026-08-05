/* cpp_newdel.c — m++ (C++) new/delete expressions and throw statements.
 *
 * ``new T`` / ``new T(args)`` lower to malloc + ctor call; ``delete p`` /
 * ``delete[] p`` lower to dtor call + free with an allocation header;
 * ``throw expr`` lowers to the exception runtime helper (see cpp_exc_*).
 *
 * Extracted from cpp_parse.c (split into per-domain submodules); entry
 * points cpp_parse_new_expr/cpp_parse_delete_expr/cpp_parse_throw_expr/
 * cpp_exc_stmt are referenced by the C front-end (expr_unary.c, stmt.c)
 * through local extern declarations.
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

/* --- new/delete expressions (C++98) ---------------------------- */

/* Ensure the file scope declares the libc allocator helper `name`
 * (`malloc` / `free`); generated code calls it directly, so the program
 * must link against a libc that provides them. */
static struct decl *
cpp_ensure_libc_fn(const char *name)
{
	extern struct scope filescope;
	struct decl *fd;

	fd = scopegetdecl(&filescope, name, 1);
	if (fd && fd->kind == DECLFUNC)
		return fd;
	if (strcmp(name, "malloc") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = mkpointertype(&typevoid, QUALNONE); /* void *malloc(size_t) */
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 1;
		ft->u.func.params = mkdecl("sz", DECLOBJECT, &typeulong,
		    QUALNONE, LINKNONE);
		ft->u.func.params->u.obj.storage = SDAUTO;
		fd = mkdecl("malloc", DECLFUNC, ft, QUALNONE, LINKEXTERN);
	} else if (strcmp(name, "free") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typevoid; /* void free(void *) */
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 1;
		ft->u.func.params = mkdecl("p", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		ft->u.func.params->u.obj.storage = SDAUTO;
		fd = mkdecl("free", DECLFUNC, ft, QUALNONE, LINKEXTERN);
	} else {
		return NULL;
	}
	fd->value = mkglobal(fd); /* global symbol slot for &malloc/&free */
	scopeputdecl(&filescope, fd);
	return fd;
}

/* Build a call expression `malloc(size)`. */
static struct expr *
cpp_malloc_expr(struct expr *size)
{
	struct decl *fd = cpp_ensure_libc_fn("malloc");
	struct expr *fn, *call;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &malloc */
	call = mkexpr(EXPRCALL, mkpointertype(&typevoid, QUALNONE), fn);
	call->u.call.args = size;
	call->u.call.nargs = 1;
	return call;
}

/* Build a call expression `free(p)`. */
static struct expr *
cpp_free_expr(struct expr *p)
{
	struct decl *fd = cpp_ensure_libc_fn("free");
	struct expr *fn, *call;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &free */
	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = p;
	call->u.call.nargs = 1;
	return call;
}

/* Build a call expression `Class_Class(thisp, args...)`, resolving the
 * constructor overload from the argument types (like an object
 * declaration `Point p(3)`).  Returns NULL when the class has no
 * matching constructor. */
static struct expr *
cpp_ctor_expr(struct type *t, struct expr *thisp, struct expr *args)
{
	extern struct scope filescope;
	const char *tag = t ? t->u.structunion.tag : NULL;
	char code[256];
	struct decl *fd, *p;
	struct expr *fn, *call, *a, **end;

	if (!tag || !cpp_has_ctor(t, tag))
		return NULL;
	cpp_mangled_name_args(t, tag, args, code, sizeof code, true);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, code, true);
	if (!fd || fd->kind != DECLFUNC) {
		char code2[256];
		cpp_mangled_name_args(t, tag, args, code2, sizeof code2, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, code2, true);
		if (fd && fd->kind == DECLFUNC)
			snprintf(code, sizeof code, "%s", code2);
	}
	if (!fd || fd->kind != DECLFUNC)
		return NULL;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_Class */
	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = thisp;
	call->u.call.nargs = 1;
	end = &thisp->next;
	/* reference parameters (copy/move ctors) receive the address of the
	 * argument; by-value parameters receive the value */
	for (a = args, p = fd->type->u.func.params ? fd->type->u.func.params->next : NULL;
	     a; a = a->next, p = p ? p->next : NULL) {
		struct expr *arg = a;
		if (p && p->type && p->type->isref)
			arg = mkunaryexpr(TBAND, a);
		*end = arg;
		end = &arg->next;
		++call->u.call.nargs;
	}
	return call;
}

/* `new T` / `new T(args)`: lower to `tmp = malloc(sizeof(T)), ctor(tmp,
 * args...), tmp` where `tmp` is a stack slot of type T*; the result is
 * the pointer (a comma expression, so the ctor side effect runs before
 * the value is used).  Array form `new T[n]` is not supported yet. */
struct expr *
cpp_parse_new_expr(struct scope *s)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);
	extern struct expr *mkassignexpr(struct expr *, struct expr *);
	extern struct qualtype declspecs(struct scope *, enum storageclass *,
	    enum funcspec *, int *);

	struct qualtype base;
	enum storageclass sc;
	int align;
	struct type *t, *pt;
	struct expr *args = NULL, **ae = &args;
	struct expr *ident, *ctor, *e, *place;
	struct decl *tmp;
	bool bracked = false; /* `new T{...}` — see the TLBRACE branch below */

	next(); /* consume 'new' */
	place = NULL;
	/* placement new: `new (ptr) T(args)` — construct at `ptr`, no
	 * allocation.  Extra placement arguments are accepted but unused. */
	if (tok.kind == TLPAREN) {
		next();
		place = assignexpr(s);
		while (tok.kind == TCOMMA) {
			next();
			(void)assignexpr(s);
		}
		expect(TRPAREN, "after placement arguments in 'new'");
	}
	base = declspecs(s, &sc, NULL, &align);
	if (!base.type)
		error_code(E_SYNTAX, &tok.loc, "expected type in 'new' expression");
	t = base.type;
	if (t->incomplete)
		error_code(E_INCOMPLETE, &tok.loc, "'new' on incomplete type");
	if (tok.kind == TLBRACK) {
		/* new T[n]: malloc(sizeof(size_t) + n*sizeof(T)), store n as a
		 * cookie before the array, then default-construct each element
		 * (for a class with a constructor) in a loop. */
		struct expr *cnt;
		next(); /* '[' */
		cnt = assignexpr(s);
		expect(TRBRACK, "after array size in 'new'");
		/* `new T[n]{list}` (array braced-init list): collect the brace
		 * elements into `args`; each is assigned to the corresponding
		 * element below, and any elements beyond the list are
		 * value-initialized (scalar -> 0). (Moved out of "not implemented".) */
		if (tok.kind == TLBRACE) {
			bracked = true; /* even an empty list `new T[n]{}` needs fill */
			if (!(t->kind == TYPESTRUCT || t->kind == TYPEUNION))
				/* scalar array: collect the flat list here; a class array's
				 * brace elements are per-element nested `{...}` sub-lists,
				 * handled (and consumed) in the class-array branch below */
				args = cpp_braced_args_collect(s);
		}
		if (!curfunc)
			error_code(E_DECL, &tok.loc, "'new' outside of a function body is not supported");
		pt = mkpointertype(t, QUALNONE);
		tmp = mkdecl("tmp", DECLOBJECT, pt, QUALNONE, LINKNONE);
		tmp->u.obj.storage = SDAUTO;
		funcinit(curfunc, tmp, NULL, false);
		ident = mkexpr(EXPRIDENT, pt, NULL);
		ident->qual = QUALNONE;
		ident->lvalue = true;
		ident->u.ident.decl = tmp;
		{
			struct decl *rawd, *nd;
			struct expr *rawe, *ne, *sz, *tot, *castc, *deref, *cp;
			/* n = cnt (evaluate the size once) */
			nd = mkdecl("__nw_n", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
			nd->u.obj.storage = SDAUTO;
			funcinit(curfunc, nd, NULL, false);
			ne = mkexpr(EXPRIDENT, &typeulong, NULL);
			ne->lvalue = true;
			ne->u.ident.decl = nd;
			funcexpr(curfunc, mkassignexpr(ne,
			    mkexpr(EXPRCAST, &typeulong, cnt)));
			/* raw = malloc(sizeof(size_t) + n * sizeof(T)) */
			rawd = mkdecl("__nw_raw", DECLOBJECT,
			    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
			rawd->u.obj.storage = SDAUTO;
			funcinit(curfunc, rawd, NULL, false);
			rawe = mkexpr(EXPRIDENT, mkpointertype(&typevoid, QUALNONE), NULL);
			rawe->lvalue = true;
			rawe->u.ident.decl = rawd;
			sz = mkexpr(EXPRBINARY, &typeulong, NULL);
			sz->op = TMUL;
			sz->u.binary.l = ne;
			sz->u.binary.r = mkconstexpr(&typeulong, t->size);
			tot = mkexpr(EXPRBINARY, &typeulong, NULL);
			tot->op = TADD;
			tot->u.binary.l = mkconstexpr(&typeulong, sizeof(size_t));
			tot->u.binary.r = sz;
			funcexpr(curfunc, mkassignexpr(rawe, cpp_malloc_expr(tot)));
			/* *(size_t*)raw = n (array-length cookie) */
			castc = mkexpr(EXPRCAST, mkpointertype(&typeulong, QUALNONE), rawe);
			deref = mkexpr(EXPRUNARY, &typeulong, castc);
			deref->op = TMUL;
			deref->lvalue = true;
			funcexpr(curfunc, mkassignexpr(deref, ne));
			/* tmp = (T*)((char*)raw + sizeof(size_t)) */
			cp = mkexpr(EXPRBINARY, mkpointertype(&typechar, QUALNONE), NULL);
			cp->op = TADD;
			cp->u.binary.l = mkexpr(EXPRCAST,
			    mkpointertype(&typechar, QUALNONE), rawe);
			cp->u.binary.r = mkconstexpr(&typeulong, sizeof(size_t));
			funcexpr(curfunc, mkassignexpr(ident,
			    mkexpr(EXPRCAST, pt, cp)));
			/* value-construct each element for a class with a ctor, or
			 * install each element's vptr for a polymorphic class with
			 * no user ctor (virtual dispatch needs it) */
			if (t->kind == TYPESTRUCT || t->kind == TYPEUNION) {
				extern bool cpp_is_abstract(struct type *);
				if (cpp_is_abstract(t))
					error_code(E_DECL, &tok.loc,
					    "cannot instantiate abstract class '%s'",
					    t->u.structunion.tag ? t->u.structunion.tag : "?");
				bool needs_elem = cpp_has_ctor(t, t->u.structunion.tag) ||
				                  t->u.structunion.poly;
				if (needs_elem && !args && !bracked) {
					struct decl *iv;
					struct expr *ie, *lt, *ptr, *inc;
					struct block *bloop, *bbody, *bdone;
					extern struct block *mkblock(char *);
					extern void funclabel(struct func *, struct block *);
					extern void funcjmp(struct func *, struct block *);
					extern struct value *funcbranch(struct func *,
					    struct expr *, struct block *, struct block *);
					iv = mkdecl("__nw_i", DECLOBJECT, &typeint,
					    QUALNONE, LINKNONE);
					iv->u.obj.storage = SDAUTO;
					funcinit(curfunc, iv, NULL, false);
					ie = mkexpr(EXPRIDENT, &typeint, NULL);
					ie->lvalue = true;
					ie->u.ident.decl = iv;
					funcexpr(curfunc, mkassignexpr(ie,
					    mkconstexpr(&typeint, 0)));
					bloop = mkblock("loop");
					bbody = mkblock("body");
					bdone = mkblock("done");
					funclabel(curfunc, bloop);
					lt = mkexpr(EXPRBINARY, &typeint, NULL);
					lt->op = TLESS;
					lt->u.binary.l = ie;
					lt->u.binary.r = mkexpr(EXPRCAST, &typeint, ne);
					funcbranch(curfunc, lt, bbody, bdone);
					funclabel(curfunc, bbody);
					/* ctor(&tmp[i]) — tmp + i*sizeof(T): the IR TADD does
					 * not scale a pointer operand (scaling happens in the
					 * C frontend's sema, which cpp bypasses), so the
					 * element stride must be applied here */
					ptr = mkexpr(EXPRBINARY, pt, NULL);
					ptr->op = TADD;
					ptr->u.binary.l = ident;
					{
						struct expr *off = mkexpr(EXPRBINARY,
						    &typeulong, NULL);
						off->op = TMUL;
						off->u.binary.l = mkexpr(EXPRCAST,
						    &typeulong, ie);
						off->u.binary.r =
						    mkconstexpr(&typeulong, t->size);
						ptr->u.binary.r = off;
					}
					ctor = NULL;
					if (cpp_has_ctor(t, t->u.structunion.tag)) {
						ctor = cpp_ctor_expr(t, ptr, NULL);
						if (!ctor)
							error_code(E_OVERLOAD, &tok.loc,
							    "no matching constructor for 'new %s[]'",
							    t->u.structunion.tag);
						funcexpr(curfunc, ctor);
					} else if (t->u.structunion.poly) {
						cpp_init_vptrs(curfunc, t, ptr);
					}
					inc = mkexpr(EXPRBINARY, &typeint, NULL);
					inc->op = TADD;
					inc->u.binary.l = ie;
					inc->u.binary.r = mkconstexpr(&typeint, 1);
					funcexpr(curfunc, mkassignexpr(ie, inc));
					funcjmp(curfunc, bloop);
					funclabel(curfunc, bdone);
				} else if (bracked) {
					/* `new T[n]{...}` class-array braced init: the outer
					 * `{` is still the current token (scalar arrays
					 * consumed it above; we left it for per-element
					 * handling).  Each brace element is either a nested
					 * `{...}` sub-list (the element's member/ctor args)
					 * or a single value expression; elements beyond the
					 * list are value-initialized (default ctor for a class,
					 * zero for scalars). */
					struct expr *a;
					struct block *bload2, *bdone2;
					int k = 0;
					next(); /* consume outer '{' */
					for (;;) {
						struct expr *elem_args = NULL, **ea = &elem_args;
						struct expr *elem_val = NULL;
						if (tok.kind == TRBRACE)
							break;
						if (k > 0)
							expect(TCOMMA, "or '}' in new array initializer");
						/* collect this element's initializer */
						if (tok.kind == TLBRACE) {
							/* nested `{1,2}`: element args */
							next(); /* consume '{' */
							for (;;) {
								if (tok.kind == TRBRACE)
									break;
								if (elem_args)
									expect(TCOMMA, "or '}' in braced element");
								*ea = assignexpr(s);
								ea = &(*ea)->next;
							}
							next(); /* consume '}' */
						} else {
							elem_val = assignexpr(s); /* single value */
						}
						/* ptr = tmp + k*sizeof(T) */
						{
							struct expr *pe = mkexpr(EXPRBINARY, pt, NULL);
							pe->op = TADD;
							pe->u.binary.l = ident;
							pe->u.binary.r = mkbinaryexpr(&tok.loc, TMUL,
							    mkconstexpr(&typeulong, (unsigned long long)k),
							    mkconstexpr(&typeulong, t->size));
							if (elem_val && elem_val->type == t) {
								/* single-value class element (`{Pt(1,2)}`):
								 * copy-initialize the heap element from the
								 * temporary — heap[k] = expr.  The temporary
								 * was constructed by its own ctor;
								 * assign its value into the array slot. */
								struct expr *dst = mkunaryexpr(TMUL, pe);
								dst->type = t;
								dst->lvalue = true;
								funcexpr(curfunc, mkassignexpr(dst, elem_val));
								if (t->u.structunion.poly)
									cpp_init_vptrs(curfunc, t, pe);
							} else if (cpp_has_ctor(t, t->u.structunion.tag)) {
								if (elem_val)
									*ea = elem_val; /* scalar/other single arg */
								struct expr *ce = cpp_ctor_expr(t, pe, elem_args);
								if (!ce)
									error_code(E_OVERLOAD, &tok.loc,
									    "no matching constructor for braced array element '%s'",
									    t->u.structunion.tag);
								funcexpr(curfunc, ce);
							} else {
								/* aggregate: positionally assign members */
								struct member *m;
								for (m = t->u.structunion.members, a = elem_args;
								     m && a; m = m->next, a = a->next) {
									struct expr *base, *dst;
									if (m->name && m->name[0] == '~')
										continue;
									if (m->type && m->type->kind == TYPEFUNC)
										continue;
									base = mkbinaryexpr(&tok.loc, TADD,
									    exprconvert(pe, &typeulong),
									    mkconstexpr(&typeulong, m->offset));
									base->type = mkpointertype(m->type, QUALNONE);
									dst = mkunaryexpr(TMUL, base);
									dst->type = m->type;
									dst->lvalue = true;
									funcexpr(curfunc, mkassignexpr(dst, a));
								}
								if (t->u.structunion.poly)
									cpp_init_vptrs(curfunc, t, pe);
							}
						}
						++k;
					}
					next(); /* consume outer '}' */
					/* value-init the remaining [k, n) elements (default
					 * ctor for a class, zero for scalars): reuse the same
					 * per-element construct loop as the un-braced path by
					 * letting the k value-constructed elements stand in.  A
					 * short list means the trailing heap slots hold the
					 * default-constructed value; for classes with a user
					 * ctor this mirrors `new T[n]`. */
					{
						struct decl *iv;
						struct expr *ie, *pe, *lt, *inc;
						struct block *bloop3, *bbody3;
						extern struct type typeint;
						iv = mkdecl("__nw_j", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
						iv->u.obj.storage = SDAUTO;
						funcinit(curfunc, iv, NULL, false);
						ie = mkexpr(EXPRIDENT, &typeint, NULL);
						ie->lvalue = true;
						ie->u.ident.decl = iv;
						funcexpr(curfunc, mkassignexpr(ie, mkconstexpr(&typeint, k)));
						bload2 = mkblock("loop3");
						bbody3 = mkblock("body3");
						bdone2 = mkblock("done3");
						funclabel(curfunc, bload2);
						lt = mkbinaryexpr(&tok.loc, TLESS, ie,
						    mkexpr(EXPRCAST, &typeint, ne));
						funcbranch(curfunc, lt, bbody3, bdone2);
						funclabel(curfunc, bbody3);
						pe = mkexpr(EXPRBINARY, pt, NULL);
						pe->op = TADD;
						pe->u.binary.l = ident;
						pe->u.binary.r = mkbinaryexpr(&tok.loc, TMUL,
						    mkexpr(EXPRCAST, &typeulong, ie),
						    mkconstexpr(&typeulong, t->size));
						if (cpp_has_ctor(t, t->u.structunion.tag)) {
							struct expr *ce2 = cpp_ctor_expr(t, pe, NULL);
							if (ce2)
								funcexpr(curfunc, ce2);
						} else if (t->u.structunion.poly) {
							cpp_init_vptrs(curfunc, t, pe);
						}
						inc = mkbinaryexpr(&tok.loc, TADD, ie,
						    mkconstexpr(&typeint, 1));
						funcexpr(curfunc, mkassignexpr(ie, inc));
						funcjmp(curfunc, bload2);
						funclabel(curfunc, bdone2);
					}
				}
		} else if (bracked) {
			/* `new T[n]{list}` scalar array braced-init: assign each
			 * brace element to the corresponding element of the heap
			 * array; if the list is shorter than n, value-initialize
			 * the remaining elements (scalar -> 0) in a runtime loop. */
			struct expr *a;
			struct decl *iv;
			struct expr *ie, *ptr, *dst, *lt, *inc;
			struct block *bloop, *bbody, *bdone;
			extern struct block *mkblock(char *);
			extern void funclabel(struct func *, struct block *);
			extern void funcjmp(struct func *, struct block *);
			extern struct value *funcbranch(struct func *,
			    struct expr *, struct block *, struct block *);
			int k = 0;
			for (a = args; a; a = a->next, ++k) {
				struct expr *off, *arg;
				/* tmp[i] = list_k (static, compile-time known) */
				ptr = mkexpr(EXPRBINARY, pt, NULL);
				ptr->op = TADD;
				ptr->u.binary.l = ident;
				off = mkbinaryexpr(&tok.loc, TMUL,
				    mkconstexpr(&typeulong, k),
				    mkconstexpr(&typeulong, t->size));
				ptr->u.binary.r = off;
				dst = mkunaryexpr(TMUL, ptr);
				dst->type = t;
				dst->lvalue = true;
				arg = exprassign(a, t);
				funcexpr(curfunc, mkassignexpr(dst, arg));
			}
			/* fill [k, n) with value-init (scalar 0) in a loop */
			iv = mkdecl("__nw_i", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
			iv->u.obj.storage = SDAUTO;
			funcinit(curfunc, iv, NULL, false);
			ie = mkexpr(EXPRIDENT, &typeint, NULL);
			ie->lvalue = true;
			ie->u.ident.decl = iv;
			funcexpr(curfunc, mkassignexpr(ie, mkconstexpr(&typeint, k)));
			bloop = mkblock("loop");
			bbody = mkblock("body");
			bdone = mkblock("done");
			funclabel(curfunc, bloop);
			lt = mkexpr(EXPRBINARY, &typeint, NULL);
			lt->op = TLESS;
			lt->u.binary.l = ie;
			lt->u.binary.r = mkexpr(EXPRCAST, &typeint, ne);
			funcbranch(curfunc, lt, bbody, bdone);
			funclabel(curfunc, bbody);
			ptr = mkexpr(EXPRBINARY, pt, NULL);
			ptr->op = TADD;
			ptr->u.binary.l = ident;
			{
				struct expr *off = mkbinaryexpr(&tok.loc, TMUL,
				    mkexpr(EXPRCAST, &typeulong, ie),
				    mkconstexpr(&typeulong, t->size));
				ptr->u.binary.r = off;
			}
			dst = mkunaryexpr(TMUL, ptr);
			dst->type = t;
			dst->lvalue = true;
			funcexpr(curfunc, mkassignexpr(dst,
			    mkconstexpr(&typeint, 0)));
			inc = mkbinaryexpr(&tok.loc, TADD, ie,
			    mkconstexpr(&typeint, 1));
			funcexpr(curfunc, mkassignexpr(ie, inc));
			funcjmp(curfunc, bloop);
			funclabel(curfunc, bdone);
		}
	}
		e = mkexpr(EXPRIDENT, pt, NULL);
		e->qual = QUALNONE;
		e->lvalue = true;
		e->u.ident.decl = tmp;
		return e;
	}
	if (tok.kind == TLBRACE) {
		/* `new T{args}` (braced-init, C++11): collect the brace elements
		 * into `args` and fall through to the scalar/class construction
		 * path below.  For a scalar this value-initializes the heap
		 * scalar exactly like `new T(arg)` (C++ [expr.new]); for a class
		 * the initializer-list / aggregate construction reuses the paren
		 * constructor path. */
		bracked = true;
		args = cpp_braced_args_collect(s);
	}
	if (tok.kind == TLPAREN) {
		next();
		while (tok.kind != TRPAREN) {
			if (args)
				expect(TCOMMA, "or ')' after 'new' argument");
			*ae = assignexpr(s);
			ae = &(*ae)->next;
		}
		next();
	}
	if (!curfunc)
		error_code(E_DECL, &tok.loc, "'new' outside of a function body is not supported");

	pt = mkpointertype(t, QUALNONE);
	if (place) {
		/* placement new: no allocation; construct at (T*)place */
		if (t->kind == TYPESTRUCT || t->kind == TYPEUNION) {
			if (cpp_has_ctor(t, t->u.structunion.tag)) {
				struct expr *thisp = mkexpr(EXPRCAST, pt, place);
				ctor = cpp_ctor_expr(t, thisp, args);
				if (!ctor)
					error_code(E_OVERLOAD, &tok.loc,
					    "no matching constructor for 'new (ptr) %s'",
					    t->u.structunion.tag);
				funcexpr(curfunc, ctor);
			} else if (args) {
				error_code(E_OVERLOAD, &tok.loc,
				    "'%s' has no constructor for 'new' with arguments",
				    t->u.structunion.tag);
			}
		} else if (args) {
			error_code(E_CTYPE, &tok.loc,
			    "'new' with arguments requires a class type");
		}
		e = mkexpr(EXPRCAST, pt, place);
		e->lvalue = true;
		return e;
	}
	/* tmp (T*) = malloc(sizeof(T)); emit the calls immediately (in parse
	 * order) and yield an expression that reads the saved pointer. */
	tmp = mkdecl("tmp", DECLOBJECT, pt, QUALNONE, LINKNONE);
	tmp->u.obj.storage = SDAUTO;
	funcinit(curfunc, tmp, NULL, false); /* allocate the pointer slot */

	ident = mkexpr(EXPRIDENT, pt, NULL);
	ident->qual = QUALNONE;
	ident->lvalue = true;
	ident->u.ident.decl = tmp;

	funcexpr(curfunc, mkassignexpr(ident,
	    cpp_malloc_expr(mkconstexpr(&typeulong, t->size))));

	if (t->kind == TYPESTRUCT || t->kind == TYPEUNION) {
		extern bool cpp_is_abstract(struct type *);
		if (cpp_is_abstract(t))
			error_code(E_DECL, &tok.loc,
			    "cannot instantiate abstract class '%s'",
			    t->u.structunion.tag ? t->u.structunion.tag : "?");
		if (cpp_has_ctor(t, t->u.structunion.tag)) {
			struct expr *thisp = mkexpr(EXPRIDENT, pt, NULL);
			thisp->qual = QUALNONE; thisp->lvalue = true;
			thisp->u.ident.decl = tmp;
			ctor = cpp_ctor_expr(t, thisp, args);
			if (!ctor)
				error_code(E_DECL, &tok.loc, "no matching constructor for 'new %s'",
				    t->u.structunion.tag);
			funcexpr(curfunc, ctor);
		} else {
			if (args) {
				if (bracked) {
					/* `new S{...}` aggregate braced-init: positionally
					 * assign the data members through the heap pointer
					 * (mirrors cpp_emit_ctor_call's aggregate path, but
					 * addresses `*tmp` instead of a stack object). */
					struct expr *a;
					struct member *m;
					for (m = t->u.structunion.members, a = args;
					     m && a; m = m->next, a = a->next) {
						struct expr *base, *dst;
						if (m->name && m->name[0] == '~')
							continue;
						if (m->type && m->type->kind == TYPEFUNC)
							continue;
						base = mkbinaryexpr(&tok.loc, TADD,
						    exprconvert(ident, &typeulong),
						    mkconstexpr(&typeulong, m->offset));
						base->type = mkpointertype(m->type, QUALNONE);
						dst = mkunaryexpr(TMUL, base);
						dst->type = m->type;
						dst->lvalue = true;
						funcexpr(curfunc, mkassignexpr(dst, a));
					}
					if (t->u.structunion.poly)
						cpp_init_vptrs(curfunc, t, ident);
				} else {
					error_code(E_DECL, &tok.loc,
					    "'%s' has no constructor for 'new' with arguments",
					    t->u.structunion.tag);
				}
			}
			/* A polymorphic class with no user ctor still needs its vptrs
			 * installed on the heap object (its bases are trivially
			 * constructed, but virtual dispatch reads the vptr). */
			if (!args && t->u.structunion.poly) {
				struct expr *thisp = mkexpr(EXPRIDENT, pt, NULL);
				thisp->qual = QUALNONE; thisp->lvalue = true;
				thisp->u.ident.decl = tmp;
				cpp_init_vptrs(curfunc, t, thisp);
			}
		}
	} else {
		/* scalar new: `new int(42)` value-initialises the heap scalar with
		 * the argument (C++ [expr.new]).  Without an argument the storage
		 * is left indeterminate, as the standard allows. */
		if (args) {
			struct expr *arg = args;
			struct expr *dp;
			extern struct expr *exprassign(struct expr *, struct type *);
			if (arg->next)
				error_code(E_CTYPE, &tok.loc,
				    "scalar 'new' takes a single value-init argument");
			dp = mkexpr(EXPRUNARY, t, ident);
			dp->op = TMUL;
			dp->lvalue = true;
			arg = exprassign(arg, t);
			funcexpr(curfunc, mkassignexpr(dp, arg));
		}
	}
	/* the new-expression's value: the pointer stored in tmp */
	e = mkexpr(EXPRIDENT, pt, NULL);
	e->qual = QUALNONE;
	e->lvalue = true;
	e->u.ident.decl = tmp;
	return e;
}

/* `delete p`: emit a destructor call `Class_dtor(p)` (for a class with a
 * destructor) followed by `free(p)`, and yield a void-typed expression.
 *
 * C++ says `delete` / `delete[]` on a null pointer is a no-op: the operand
 * is evaluated once, and neither a destructor nor the allocator may be
 * touched for a null pointer.  `free(NULL)` is itself a no-op, so a scalar
 * `delete` only needs its destructor guarded; `delete[]` additionally
 * reads the array-length cookie at `(char*)p - sizeof(size_t)` and frees
 * `(char*)p - sizeof(size_t)`, both of which must stay inside the
 * null-guard (defect Q). */
struct expr *
cpp_parse_delete_expr(struct scope *s)
{
	extern struct scope filescope;
	extern struct func *curfunc;

	struct expr *e, *dtor, *fn;
	struct type *t;
	const char *tag;
	char mname[256];
	struct decl *fd;
	extern struct block *mkblock(char *);
	extern void funclabel(struct func *, struct block *);
	extern void funcjmp(struct func *, struct block *);
	extern struct value *funcbranch(struct func *,
	    struct expr *, struct block *, struct block *);

	next(); /* consume 'delete' */
	if (tok.kind == TLBRACK) {
		/* delete[] p: read the array-length cookie stored by `new T[n]`,
		 * destruct each element (for a class with a destructor) in a
		 * loop, then free the raw allocation.  The whole body runs
		 * only when p != NULL. */
		struct expr *ne, *cp, *castc, *deref, *fp, *te, *nul;
		struct decl *nd, *td;
		struct block *bload, *bdone;
		next(); /* consume '[' */
		expect(TRBRACK, "after 'delete['");
		e = castexpr(s);
		if (!e || e->type->kind != TYPEPOINTER)
			error_code(E_CTYPE, &tok.loc, "delete operand must be a pointer");
		t = e->type->base;
		/* __dl_fp = (void*)0; set to (char*)e - sizeof(size_t) only
		 * inside the null-guard, so the returned free(NULL) is a
		 * no-op when e is null */
		td = mkdecl("__dl_fp", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		td->u.obj.storage = SDAUTO;
		funcinit(curfunc, td, NULL, false);
		te = mkexpr(EXPRIDENT, td->type, NULL);
		te->lvalue = true;
		te->u.ident.decl = td;
		funcexpr(curfunc, mkassignexpr(te, mkconstexpr(td->type, 0)));
		/* if (e != 0) { ... } */
		nul = mkexpr(EXPRBINARY, &typeint, NULL);
		nul->op = TNEQ;
		nul->u.binary.l = e;
		nul->u.binary.r = mkexpr(EXPRCONST, &typenullptr, NULL);
		nul->u.binary.r->u.constant.u = 0;
		bload = mkblock("body");
		bdone = mkblock("done");
		funcbranch(curfunc, nul, bload, bdone);
		funclabel(curfunc, bload);
		/* n = *(size_t*)((char*)p - sizeof(size_t)) */
		nd = mkdecl("__dl_n", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
		nd->u.obj.storage = SDAUTO;
		funcinit(curfunc, nd, NULL, false);
		ne = mkexpr(EXPRIDENT, &typeulong, NULL);
		ne->lvalue = true;
		ne->u.ident.decl = nd;
		cp = mkexpr(EXPRBINARY, mkpointertype(&typechar, QUALNONE), NULL);
		cp->op = TSUB;
		cp->u.binary.l = mkexpr(EXPRCAST, mkpointertype(&typechar, QUALNONE), e);
		cp->u.binary.r = mkconstexpr(&typeulong, sizeof(size_t));
		castc = mkexpr(EXPRCAST, mkpointertype(&typeulong, QUALNONE), cp);
		deref = mkexpr(EXPRUNARY, &typeulong, castc);
		deref->op = TMUL;
		deref->lvalue = true;
		funcexpr(curfunc, mkassignexpr(ne, deref));
		/* destruct each element for a class with a destructor */
		if (t && (t->kind == TYPESTRUCT || t->kind == TYPEUNION) &&
		    (tag = t->u.structunion.tag) && cpp_has_dtor(t)) {
			struct decl *iv;
			struct expr *ie, *lt, *inc, *dn;
			struct block *bloop, *bbody;
			iv = mkdecl("__dl_i", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
			iv->u.obj.storage = SDAUTO;
			funcinit(curfunc, iv, NULL, false);
			ie = mkexpr(EXPRIDENT, &typeint, NULL);
			ie->lvalue = true;
			ie->u.ident.decl = iv;
			funcexpr(curfunc, mkassignexpr(ie, mkconstexpr(&typeint, 0)));
			bloop = mkblock("loop");
			bbody = mkblock("body");
			funclabel(curfunc, bloop);
			lt = mkexpr(EXPRBINARY, &typeint, NULL);
			lt->op = TLESS;
			lt->u.binary.l = ie;
			lt->u.binary.r = mkexpr(EXPRCAST, &typeint, ne);
			funcbranch(curfunc, lt, bbody, bdone);
			funclabel(curfunc, bbody);
			/* dtor(&p[i]) — p + i*sizeof(T) (element stride applied
			 * here: the IR TADD does not scale pointer operands) */
			snprintf(mname, sizeof mname, "%s_dtor", tag);
			fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
			if (fd && fd->kind == DECLFUNC) {
				struct expr *thp;
				thp = mkexpr(EXPRBINARY, e->type, NULL);
				thp->op = TADD;
				thp->u.binary.l = e;
				{
					struct expr *off = mkexpr(EXPRBINARY,
					    &typeulong, NULL);
					off->op = TMUL;
					off->u.binary.l = mkexpr(EXPRCAST,
					    &typeulong, ie);
					off->u.binary.r =
					    mkconstexpr(&typeulong, t->size);
					thp->u.binary.r = off;
				}
				fn = mkexpr(EXPRIDENT, fd->type, NULL);
				fn->u.ident.decl = fd;
				fn = decay(fn);
				dn = mkexpr(EXPRCALL, &typevoid, fn);
				dn->u.call.args = thp;
				dn->u.call.nargs = 1;
				funcexpr(curfunc, dn);
			}
			inc = mkexpr(EXPRBINARY, &typeint, NULL);
			inc->op = TADD;
			inc->u.binary.l = ie;
			inc->u.binary.r = mkconstexpr(&typeint, 1);
			funcexpr(curfunc, mkassignexpr(ie, inc));
			funcjmp(curfunc, bloop);
		}
		/* __dl_fp = (char*)p - sizeof(size_t) */
		fp = mkexpr(EXPRBINARY, mkpointertype(&typechar, QUALNONE), NULL);
		fp->op = TSUB;
		fp->u.binary.l = mkexpr(EXPRCAST, mkpointertype(&typechar, QUALNONE), e);
		fp->u.binary.r = mkconstexpr(&typeulong, sizeof(size_t));
		funcexpr(curfunc, mkassignexpr(te, mkexpr(EXPRCAST,
		    mkpointertype(&typevoid, QUALNONE), fp)));
		funcjmp(curfunc, bdone);
		funclabel(curfunc, bdone);
		/* free(__dl_fp): NULL when p was NULL (no-op), the raw
		 * allocation otherwise.  Emitted as the returned call so the
		 * caller's funcexpr (e.g. the expression statement) runs it. */
		return cpp_free_expr(te);
	}
	e = castexpr(s);
	if (!e || e->type->kind != TYPEPOINTER)
		error_code(E_CTYPE, &tok.loc, "delete operand must be a pointer");
	t = e->type->base;
	if (t && (t->kind == TYPESTRUCT || t->kind == TYPEUNION) &&
	    (tag = t->u.structunion.tag) && cpp_has_dtor(t)) {
		/* dtor runs only for a non-null pointer; free(NULL) is a no-op,
		 * so the destructor guard suffices */
		struct block *bload, *bdone;
		struct expr *nul;
		nul = mkexpr(EXPRBINARY, &typeint, NULL);
		nul->op = TNEQ;
		nul->u.binary.l = e;
		nul->u.binary.r = mkexpr(EXPRCONST, &typenullptr, NULL);
		nul->u.binary.r->u.constant.u = 0;
		bload = mkblock("body");
		bdone = mkblock("done");
		funcbranch(curfunc, nul, bload, bdone);
		funclabel(curfunc, bload);
		{
			/* A virtual destructor must dispatch on the object's dynamic
			 * type through its vtable (a `B*` that points to a `D`
			 * object runs `D_dtor`), so deleting through a base pointer
			 * does not leak the derived part.  A non-virtual destructor
			 * resolves statically to the pointer's static type. */
			struct member *dm, *vdm = NULL;
			for (dm = t->u.structunion.members; dm; dm = dm->next)
				if (dm->name && dm->name[0] == '~' &&
				    dm->type && dm->type->kind == TYPEFUNC &&
				    dm->is_virtual) {
					vdm = dm;
					break;
				}
			if (vdm && vdm->vslot >= 0) {
				struct expr *vc = cpp_make_vcall(e, t, vdm, vdm->vslot);
				dtor = mkexpr(EXPRCALL, &typevoid, vc);
				dtor->u.call.args = e;
				dtor->u.call.nargs = 1;
				funcexpr(curfunc, dtor);
			} else {
				/* the dtor's vtable slot may be registered under the
				 * mangled member name `dtor` (not the `~Class` marker),
				 * so a cached member->vslot can be -1 even though the slot
				 * exists; look it up by name. */
				struct cpp_vslot *vs;
				int dslot = -1;
				for (vs = t->u.structunion.vslots; vs; vs = vs->next)
					if (vs->name && strncmp(vs->name, "dtor", 4) == 0) {
						dslot = vs->index;
						break;
					}
				if (vdm && dslot >= 0) {
					struct expr *vc = cpp_make_vcall(e, t, vdm, dslot);
					dtor = mkexpr(EXPRCALL, &typevoid, vc);
					dtor->u.call.args = e;
					dtor->u.call.nargs = 1;
					funcexpr(curfunc, dtor);
				} else {
					snprintf(mname, sizeof mname, "%s_dtor", tag);
					fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
					if (fd && fd->kind == DECLFUNC) {
						fn = mkexpr(EXPRIDENT, fd->type, NULL);
						fn->u.ident.decl = fd;
						fn = decay(fn); /* &Class_dtor */
						dtor = mkexpr(EXPRCALL, &typevoid, fn);
						dtor->u.call.args = e;
						dtor->u.call.nargs = 1;
						funcexpr(curfunc, dtor);
					}
				}
			}
		}
		funcjmp(curfunc, bdone);
		funclabel(curfunc, bdone);
	}
	/* the free(p) call expression is returned so the caller's funcexpr
	 * (e.g. the expression statement) executes it; a void EXPRCALL is a
	 * safe void-typed result */
	return cpp_free_expr(e);
}

/* --- C++ exceptions (basic frontend support) ---------------------------- */

/* C++ exception runtime helpers (minimal, non-ABI self-owned interface).
 * The landingpad/unwind backend that would route a thrown exception to a
 * catch block is not yet implemented; `_meuos_exc_throw` arms the
 * exception slot and aborts for now (forward-compatible with a future
 * unwinder).  `_meuos_exc_typecode` maps a C++ type to an integer
 * discriminant used for catch-type matching. */
struct decl *
cpp_ensure_exc_fn(const char *name)
{
	extern struct scope filescope;
	struct decl *fd = scopegetdecl(&filescope, name, 1);
	if (fd && fd->kind == DECLFUNC)
		return fd;
	if (strcmp(name, "_meuos_exc_throw") == 0) {
		/* void _meuos_exc_throw(int typecode, unsigned long long value) */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p2;
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 2;
		p2 = mkdecl("typecode", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		p2->u.obj.storage = SDAUTO;
		p2->next = mkdecl("value", DECLOBJECT, &typeullong, QUALNONE,
		    LINKNONE);
		p2->next->u.obj.storage = SDAUTO;
		ft->u.func.params = p2;
		fd = mkdecl("_meuos_exc_throw", DECLFUNC, ft, QUALNONE, LINKEXTERN);
	} else {
		return NULL;
	}
	fd->value = mkglobal(fd);
	fd->u.func.isnoreturn = true; /* throwing never returns normally */
	scopeputdecl(&filescope, fd);
	return fd;
}

/* Integer type discriminant for catch-type matching.  By default every
 * integer family maps to 0 (int); a future multi-type ABI would assign
 * distinct codes per type. */
static int
cpp_exc_typecode(struct type *t)
{
	(void)t;
	return 0;
}

/* Build a call `_meuos_exc_throw(typecode, value)`. */
static struct expr *
cpp_exc_throw_call(struct type *t, struct expr *value)
{
	struct decl *fd = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call, *a1, *a2;

	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	a1 = mkconstexpr(&typeint, cpp_exc_typecode(t));
	a2 = value ? mkexpr(EXPRCAST, &typeullong, value)
	           : mkconstexpr(&typeullong, 0);
	a2->next = NULL;
	a1->next = a2;
	call->u.call.args = a1;
	call->u.call.nargs = 2;
	return call;
}

/* C++ `try`/`catch` statement.  Parsed and recognised (so it is no longer
 * reported as an undeclared identifier), but the landingpad / .eh_frame
 * unwinder backend that routes a thrown exception to a catch block is not
 * yet implemented — so we emit a clear diagnostic rather than silent
 * miscompilation. */
void
cpp_exc_stmt(struct func *f, struct scope *s)
{
	extern void stmt(struct func *, struct scope *);
	extern void next(void);

	if (cpp_tok_kind() != CPP_TTRY)
		return;
	next(); /* consume 'try' */
	error_tok_code(E_TEMPLATE, &tok,
	    "'try'/'catch' exception handling requires the landingpad unwinder backend (not yet implemented in mcc)");
	/* never reached */
	stmt(f, s);
}

/* C++ `throw` expression (`throw expr;` or bare `throw;` rethrow).
 * Lowered to a call to the exception runtime. */
struct expr *
cpp_parse_throw_expr(struct scope *s)
{
	extern struct expr *assignexpr(struct scope *);
	struct expr *e = NULL;

	next(); /* consume 'throw' */
	if (tok.kind != TSEMICOLON && tok.kind != TEOF)
		e = assignexpr(s);
	return cpp_exc_throw_call(e ? e->type : &typeint, e);
}
