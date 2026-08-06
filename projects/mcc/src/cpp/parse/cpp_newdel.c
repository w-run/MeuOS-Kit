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
		fd->u.func.isnoreturn = true;
	} else if (strcmp(name, "_meuos_exc_throw_obj") == 0) {
		/* void _meuos_exc_throw_obj(int typecode, size_t size, size_t align,
		 *     void (*copy)(void*,const void*), void (*dtor)(void*),
		 *     int offset_to_base, const void *obj)
		 * Phase-4 object payload (exc-phase4-object-payload.md).  libc
		 * runtime packs meta + heap-copies the object + destroys the source
		 * temporary.  copy/dtor are NULL for trivial classes (runtime uses
		 * memcpy / no dtor). */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p1, *pp;
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 7;
		p1 = mkdecl("typecode", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		p1->u.obj.storage = SDAUTO;
		pp = mkdecl("size", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next = pp;
		pp = mkdecl("align", DECLOBJECT, &typeulong, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next = pp;
		pp = mkdecl("copy", DECLOBJECT,
		    mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next = pp;
		pp = mkdecl("dtor", DECLOBJECT,
		    mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next = pp;
		pp = mkdecl("offset", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next->next = pp;
		pp = mkdecl("obj", DECLOBJECT,
		    mkpointertype(&typevoid, QUALNONE), QUALNONE, LINKNONE);
		pp->u.obj.storage = SDAUTO;
		p1->next->next->next->next->next->next = pp;
		ft->u.func.params = p1;
		fd = mkdecl("_meuos_exc_throw_obj", DECLFUNC, ft, QUALNONE,
		    LINKEXTERN);
		fd->u.func.isnoreturn = true;
	} else if (strcmp(name, "_meuos_exc_try_begin") == 0) {
		/* void _meuos_exc_try_begin(_meuos_exc_frame *) */
		struct type *ft = mktype(TYPEFUNC, 0);
		struct decl *p0;
		struct type *frame_t =
		    scopegettag(&filescope, "_meuos_exc_frame", true);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 1;
		p0 = mkdecl("frame", DECLOBJECT,
		            frame_t ? mkpointertype(frame_t, QUALNONE)
		                    : mkpointertype(&typevoid, QUALNONE),
		            QUALNONE, LINKNONE);
		p0->u.obj.storage = SDAUTO;
		ft->u.func.params = p0;
		fd = mkdecl("_meuos_exc_try_begin", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_try_end") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_try_end", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_type") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeint;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_type", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_value") == 0) {
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeullong;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_value", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_obj") == 0) {
		/* const void *_meuos_exc_caught_obj(void) — object payload */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = mkpointertype(&typevoid, QUALCONST);
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_obj", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_free") == 0) {
		/* void _meuos_exc_caught_free(void) */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typevoid;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_free", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else if (strcmp(name, "_meuos_exc_caught_is_obj") == 0) {
		/* int _meuos_exc_caught_is_obj(void) */
		struct type *ft = mktype(TYPEFUNC, 0);
		ft->base = &typeint;
		ft->u.func.isvararg = false;
		ft->u.func.nparam = 0;
		fd = mkdecl("_meuos_exc_caught_is_obj", DECLFUNC, ft, QUALNONE,
		            LINKEXTERN);
	} else {
		return NULL;
	}
	fd->value = mkglobal(fd);
	scopeputdecl(&filescope, fd);
	return fd;
}

/* Integer type discriminant for catch-type matching.
 *
 * Phase 2: a type->code registry.  Each distinct type (by struct type
 * pointer — builtin singletons like &typeint and the canonical struct type
 * from scopegettag) is assigned a stable, monotonically increasing code, so
 * a `throw` of type T and a `catch (T e)` resolve to the *same* code and
 * the front-end catch sequence matches on it.  This is pure-front-end: the
 * libc runtime keeps an opaque int slot (exc_typecode) and never needs to
 * change.  (Base-class catch / integer promotion widening is phase 3+.)
 */
struct exc_tc_reg { struct type *t; int code; };
static struct exc_tc_reg exc_tc_regs[64];
static int exc_tc_n = 0;

static int
cpp_exc_typecode(struct type *t)
{
	int i;
	for (i = 0; i < exc_tc_n; i++)
		if (exc_tc_regs[i].t == t)
			return exc_tc_regs[i].code;
	if (exc_tc_n >= 64)
		return 0;             /* fallback; table exhausted is unrealistic */
	exc_tc_regs[exc_tc_n].t = t;
	exc_tc_regs[exc_tc_n].code = exc_tc_n + 1;  /* code 0 reserved: no type */
	return exc_tc_regs[exc_tc_n++].code;
}

/* Phase-4 object-payload helpers (exc-phase4-object-payload.md).
 * - exc_has_trivial_copy/dtor: whether the class type needs no user copy /
 *   destructor (trivial aggregate -> runtime memcpy / no destruction).
 *   A class with no declared constructors/destructors is trivial for the
 *   exception payload (we copy its bytes and never destroy it).
 * - exc_base_offset: offset of the first base subobject (for base-catch
 *   slicing); 0 for non-derived.
 * - cpp_exc_throw_call_scalar: the legacy class-throw path that emits
 *   `_meuos_exc_throw(tc, 0)` (type code only, no member payload). */
static int exc_has_trivial_copy(struct type *t);
static int exc_has_trivial_dtor(struct type *t);
static int exc_base_offset(struct type *t);
static int exc_member_is_base(struct member *m);
static struct expr *cpp_exc_throw_call_scalar(struct type *t, struct expr *value);

static int
exc_has_trivial_dtor(struct type *t)
{
	/* No user-declared destructor (a member whose name begins with '~' or
	 * an explicit dtor function) -> no destruction required on the payload. */
	return !cpp_has_dtor(t);
}

static int
exc_has_trivial_copy(struct type *t)
{
	/* Byte-copyable when the class declares no user destructor/constructor.
	 * (User copy/move ctors are phase-4b; for the first increment a class
	 * with no dtor is treated as copyable, and a user-constructed class
	 * falls back to the legacy zero-slot path.) */
	return exc_has_trivial_dtor(t);
}

static int
exc_base_offset(struct type *t)
{
	/* Offset of the first base sub-object within an instance of `t`, or 0
	 * when `t` has no base (so the throw carries the full object).  Bases
	 * are inserted by the class-body parser as anonymous members (name ==
	 * NULL) before any data member (see cpp_parse.c around `addmember`,
	 * with bases[]); for `struct D : B`, the first member is the Base
	 * subobject at offset 0; for `struct D : A, B` the first member is
	 * A at offset 0 (B sits at sizeof(A), reported here for the first
	 * base only).  The hidden vptr (cpp_insert_vptr) carries the name
	 * "__vptr", so the anonymous test naturally excludes it. */
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return 0;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (exc_member_is_base(m))
			return (int)m->offset;
	}
	return 0;
}

/* A member is a base placeholder when the front-end registered it as the
 * carrier of a base-class sub-object: anonymous (name == NULL) and of a
 * class type (TYPESTRUCT / TYPEUNION).  Anonymous unions that are NOT a
 * base do not exist in mcc (the parser only inserts name==NULL members
 * for the base list), so this check is precise.  The hidden vptr member
 * inserted by cpp_insert_vptr has name "__vptr" and is excluded by the
 * anonymous check. */
static int
exc_member_is_base(struct member *m)
{
	if (!m || !m->type)
		return 0;
	if (m->name)
		return 0;
	return m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION;
}

/* Offset of the `base` sub-object within an instance of class `derived`,
 * for base-subobject slicing in an exception catch.  The base appears as
 * an anonymous member (name==NULL); with multiple/chain inheritance the
 * base may sit at a non-zero offset (e.g. the 2nd base in `struct D : A,
 * B` is at offset sizeof(A)).  Recurses through nested anonymous base
 * members to sum the path.  Returns (unsigned long long)-1 when `derived`
 * is NOT derived from `base` (callers guard with cpp_is_derived first). */
static unsigned long long
exc_base_slice_offset(struct type *derived, struct type *base)
{
	struct member *m;

	if (!derived || !base)
		return (unsigned long long)-1;
	if (derived == base)
		return 0;
	if (derived->kind != TYPESTRUCT && derived->kind != TYPEUNION)
		return (unsigned long long)-1;
	for (m = derived->u.structunion.members; m; m = m->next) {
		unsigned long long sub;
		if (m->name || !m->type) /* virtual/regular members have a name */
			continue;
		if (m->type == base)
			return m->offset;
		sub = exc_base_slice_offset(m->type, base);
		if (sub != (unsigned long long)-1)
			return m->offset + sub;
	}
	return (unsigned long long)-1;
}

static struct expr *
cpp_exc_throw_call_scalar(struct type *t, struct expr *value)
{
	struct decl *fds = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call, *a1, *a2;
	if (!fds)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fds->type, NULL);
	fn->u.ident.decl = fds;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	a1 = mkconstexpr(&typeint, cpp_exc_typecode(t));
	a2 = mkconstexpr(&typeullong, 0); /* class: type code only (legacy) */
	a1->next = a2;
	call->u.call.args = a1;
	call->u.call.nargs = 2;
	return call;
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
	if (value && (value->type->kind == TYPESTRUCT ||
	              value->type->kind == TYPEUNION)) {
		/* Phase-4 object payload: when the object runtime is in scope,
		 * route a class-typed exception through _meuos_exc_throw_obj so the
		 * object travels (heap-copied by the runtime), not just the type
		 * code.  Trivial classes (no user copy ctor / no dtor) pass
		 * copy=NULL, dtor=NULL and the runtime performs a plain
		 * size-byte memcpy / no destruction (exc-phase4-object-payload.md).
		 * Fall back to the old zero-slot scheme when the runtime is not yet
		 * linked in so existing scalar/tests keep working. */
		struct decl *fdo;
		/* Use the object path ONLY when the program actually declares
		 * `_meuos_exc_throw_obj` (i.e. includes the phase-4 runtime).
		 * scopegetdecl (non-creating) checks the user's decl: if absent we
		 * must NOT synthesize+emit throw_obj, or programs that don't link
		 * the object runtime would get an undefined reference. */
		fdo = scopegetdecl(&filescope, "_meuos_exc_throw_obj", 1);
		if (fdo && fdo->kind == DECLFUNC) {
			struct expr *fn2, *call2, *arg, *a, *aobj;
			struct type *pt = mkpointertype(t, QUALNONE);
			struct type *tcode_t = &typeint;
			int ndecl = exc_has_trivial_copy(t) && exc_has_trivial_dtor(t);
			int i;
			(void)call; (void)fd; (void)a2;
			fn2 = mkexpr(EXPRIDENT, fdo->type, NULL);
			fn2->u.ident.decl = fdo;
			fn2 = decay(fn2);
			call2 = mkexpr(EXPRCALL, &typevoid, fn2);
			arg = mkconstexpr(tcode_t, cpp_exc_typecode(t));
			arg->next = mkconstexpr(&typeulong, t->size);
			arg->next->next = mkconstexpr(&typeulong, t->align);
			/* copy / dtor: NULL for now (trivial); non-trivial thunks are
			 * phase-4b — fall back to zero-slot below. */
			if (!ndecl)
				/* non-trivial ctor/dtor thunks not yet generated: keep
				 * the old documented zero-slot behaviour for this stage. */
				return cpp_exc_throw_call_scalar(t, value);
			arg->next->next->next =
			    mkconstexpr(mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), 0);
			arg->next->next->next->next =
			    mkconstexpr(mkpointertype(mktype(TYPEFUNC, 0), QUALNONE), 0);
			arg->next->next->next->next->next =
			    mkconstexpr(&typeint, exc_base_offset(t));
			/* obj: address of the throw-temporary */
			aobj = mkunaryexpr(TBAND, value);
			aobj->type = pt;
			arg->next->next->next->next->next->next = aobj;
			call2->u.call.args = arg;
			call2->u.call.nargs = 7;
			return call2;
		}
		(void)fd; (void)call; (void)a1; (void)a2;
		return cpp_exc_throw_call_scalar(t, value);
	} else {
		a2 = value ? mkexpr(EXPRCAST, &typeullong, value)
		           : mkconstexpr(&typeullong, 0);
	}
	a2->next = NULL;
	a1->next = a2;
	call->u.call.args = a1;
	call->u.call.nargs = 2;
	return call;
}

/* Build a call `_meuos_exc_throw(typecode_expr, value_expr)` with already
 * computed operands (used by bare rethrow `throw;`). */
static struct expr *
cpp_exc_throw_call2(struct expr *tcode, struct expr *value)
{
	struct decl *fd = cpp_ensure_exc_fn("_meuos_exc_throw");
	struct expr *fn, *call;

	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, &typevoid, fn);
	value = exprconvert(value, &typeullong);
	value->next = NULL;
	tcode->next = value;
	call->u.call.args = tcode;
	call->u.call.nargs = 2;
	return call;
}

/* C++ `try`/`catch` statement.  Parsed and recognised (so it is no longer
 * reported as an undeclared identifier), but the landingpad / .eh_frame
 * unwinder backend that routes a thrown exception to a catch block is not
 * yet implemented — so we emit a clear diagnostic rather than silent
 * miscompilation. */
/* Build a call to a MeuOS exception-runtime helper by name (all from
 * <meuos_exc.h>: _meuos_exc_try_begin/try_end/caught_type/caught_value),
 * returning its value or void as a funcexpr'd statement.  Falls back to
 * cpp_ensure_exc_fn for the throw helper. */
static struct expr *
cpp_exc_helper_call(const char *name, struct type *ret,
                    struct expr *arg1, struct expr *arg2)
{
	extern struct scope filescope;
	struct decl *fd = cpp_ensure_exc_fn(name);
	struct expr *fn, *call;
	if (!fd)
		return mkexpr(EXPRCONST, &typevoid, NULL);
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, ret, fn);
	call->u.call.args = arg1;
	call->u.call.nargs = arg1 ? 1 : 0;
	if (arg2) {
		arg1->next = arg2;
		call->u.call.nargs = 2;
	}
	return call;
}

/* Build a `T*` slice-view pointer for the class catch parameter of type
 * `ctype`, re-aiming the runtime-carried object at the base sub-object that
 * actually matched the catch.  The libc carries the full thrown object (the
 * registered derived type D); catch(Base) matches that D via the typecode
 * skew used by cpp_is_derived, so the Base sub-object sits at a non-zero
 * offset in D (mcc-side slice; independent of libc's offset argument).  A
 * local `__exc_slice` offset is selected by branching on
 * _meuos_exc_caught_type() over every registered type derived from ctype
 * (0 when the catch type itself matched / the base is at offset 0), then we
 * build `(T*)((char*)_meuos_exc_caught_obj() + __exc_slice)`.  All the
 * statements (decl + the if/else emitters) are emitted into `f` first; the
 * returned expression is the final slice pointer. */
static struct expr *
exc_slice_ptr(struct func *f, struct type *ctype)
{
	extern struct block *mkblock(char *);
	extern void funclabel(struct func *, struct block *);
	extern struct value *funcbranch(struct func *, struct expr *,
	    struct block *, struct block *);
	extern void funcjmp(struct func *, struct block *);
	extern void funcinit(struct func *, struct decl *, struct init *, bool);
	struct expr *ct, *se, *co, *cvs;
	struct decl *sd;
	int i;

	/* local unsigned long long __exc_slice = 0; */
	sd = mkdecl("__exc_slice", DECLOBJECT,
	    &typeulong, QUALNONE, LINKNONE);
	sd->u.obj.storage = SDAUTO;
	funcinit(f, sd, NULL, false);
	se = mkexpr(EXPRIDENT, &typeulong, NULL);
	se->lvalue = true;
	se->u.ident.decl = sd;
	ct = NULL;
	for (i = 0; i < exc_tc_n; i++) {
		struct block *bsy, *bsn;
		unsigned long long soff;
		if (exc_tc_regs[i].t == ctype)
			continue;
		if (!cpp_is_derived(exc_tc_regs[i].t, ctype))
			continue;
		soff = exc_base_slice_offset(exc_tc_regs[i].t, ctype);
		if (soff == (unsigned long long)-1)
			continue;
		if (!ct)
			ct = cpp_exc_helper_call("_meuos_exc_caught_type",
			    &typeint, NULL, NULL);
		bsy = mkblock("exc_slice_y");
		bsn = mkblock("exc_slice_n");
		funcbranch(f,
		    mkbinaryexpr(&tok.loc, TEQL, ct,
		        mkconstexpr(&typeint, exc_tc_regs[i].code)),
		    bsy, bsn);
		funclabel(f, bsy);
		funcexpr(f, mkassignexpr(se,
		    mkconstexpr(&typeulong, soff)));
		funcjmp(f, bsn);
		funclabel(f, bsn);
	}
	co = cpp_exc_helper_call("_meuos_exc_caught_obj",
	    mkpointertype(&typevoid, QUALCONST), NULL, NULL);
	/* (T*)((char*)co + __exc_slice) */
	cvs = mkexpr(EXPRBINARY,
	    mkpointertype(&typechar, QUALNONE), NULL);
	cvs->op = TADD;
	cvs->u.binary.l = mkexpr(EXPRCAST,
	    mkpointertype(&typechar, QUALNONE), co);
	cvs->u.binary.r = se;
	cvs = mkexpr(EXPRCAST,
	    mkpointertype(ctype, QUALNONE), cvs);
	return cvs;
}

/* C++ `try { ... } catch (T e) { ... }` — lowered onto the libc
 * setjmp/longjmp exception runtime (meuos_exc.h): declare a local
 * `_meuos_exc_frame`, setjmp into it, register with try_begin, branch on
 * the setjmp return: normal path runs the try body then try_end (pop); the
 * longjmp path (r != 0) matches the caught type and, on hit, copy-inits the
 * catch parameter from caught_value.  Uncaught type falls to a rethrow for
 * now (phases 3-4 make this the precise multi-catch dispatch).
 *
 * The program must `#include <meuos_exc.h>` so `_meuos_exc_frame` and the
 * helper declarations are in scope.
 */
void
cpp_exc_stmt(struct func *f, struct scope *s)
{
	extern struct scope filescope;
	extern void stmt(struct func *, struct scope *);
	extern struct scope *mkscope(struct scope *);
	extern struct scope *delscope(struct scope *);
	extern void next(void);
	extern struct block *mkblock(char *);
	extern void funclabel(struct func *, struct block *);
	extern struct value *funcbranch(struct func *, struct expr *,
	    struct block *, struct block *);
	extern void funcjmp(struct func *, struct block *);
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	struct type *frame_t;
	struct expr *frame_e, *addr, *st, *r_e, *cond, *cv, *casted;
	struct decl *frame_d, *r_d, *fd_setjmp, *ed;
	struct block *bcaught, *bnormal, *bjoin;
	enum typequal tq = QUALNONE;
	struct type *ctype = NULL;
	int catch_is_ref = 0;
	char name[256];
	int catch_tc;

	if (cpp_tok_kind() != CPP_TTRY)
		return;
	/* The frame struct comes from <meuos_exc.h> (declared in filescope). */
	frame_t = scopegettag(&filescope, "_meuos_exc_frame", true);
	if (!frame_t || (frame_t->kind != TYPESTRUCT &&
	                 frame_t->kind != TYPEUNION))
		error_tok_code(E_TEMPLATE, &tok,
		    "try/catch requires '#include <meuos_exc.h>' (defines _meuos_exc_frame)");
	next(); /* consume 'try' */

	s = mkscope(s);

	/* local `_meuos_exc_frame frame;` */
	frame_d = mkdecl("frame", DECLOBJECT, frame_t, QUALNONE, LINKNONE);
	frame_d->u.obj.storage = SDAUTO;
	funcinit(f, frame_d, NULL, false);
	frame_e = mkexpr(EXPRIDENT, frame_t, NULL);
	frame_e->lvalue = true;
	frame_e->u.ident.decl = frame_d;

	/* local `int __exc_r;` */
	r_d = mkdecl("__exc_r", DECLOBJECT, &typeint, QUALNONE, LINKNONE);
	r_d->u.obj.storage = SDAUTO;
	funcinit(f, r_d, NULL, false);
	r_e = mkexpr(EXPRIDENT, &typeint, NULL);
	r_e->lvalue = true;
	r_e->u.ident.decl = r_d;

	/* __exc_r = setjmp(frame.env)   (env is the first member, offset 0) */
	addr = mkunaryexpr(TBAND, frame_e);
	fd_setjmp = scopegetdecl(&filescope, "setjmp", true);
	if (fd_setjmp && fd_setjmp->kind == DECLFUNC) {
		struct expr *fn, *call;
		fn = mkexpr(EXPRIDENT, fd_setjmp->type, NULL);
		fn->u.ident.decl = fd_setjmp;
		fn = decay(fn);
		call = mkexpr(EXPRCALL, &typeint, fn);
		call->u.call.args = addr;
		call->u.call.nargs = 1;
		funcexpr(f, mkassignexpr(r_e, call));
	} else {
		error_tok_code(E_TEMPLATE, &tok,
		    "setjmp not declared (meuos_exc.h requires <setjmp.h>)");
	}

	/* if (__exc_r != 0) -> caught branch, else try body */
	cond = mkbinaryexpr(&tok.loc, TNEQ, r_e,
	                    mkconstexpr(&typeint, 0));
	bcaught = mkblock("exc_caught");
	bnormal = mkblock("exc_normal");
	bjoin = mkblock("exc_join");
	funcbranch(f, cond, bcaught, bnormal);

	/* normal (r == 0): register the handler, run the try body, then pop.
	 * try_begin is only called on the first setjmp pass (r == 0); an
	 * exceptional longjmp returns with r != 0 straight into the caught
	 * branch WITHOUT re-registering — re-registering would re-push this
	 * frame and make a `throw;` rethrow re-enter this same catch (loop). */
	funclabel(f, bnormal);
	st = cpp_exc_helper_call("_meuos_exc_try_begin", &typevoid,
	                          frame_e, NULL);
	funcexpr(f, st);
	stmt(f, s);
	funcexpr(f, cpp_exc_helper_call("_meuos_exc_try_end", &typevoid,
	                                NULL, NULL));
	funcjmp(f, bjoin);

	/* caught (r != 0): dispatch over the catch sequence by type code */
	funclabel(f, bcaught);
	{
		struct block *bnext = NULL;
for (;;) {
		struct block *bhit, *bmiss;
		struct expr *ctx, *cnd;
		if (cpp_tok_kind() != CPP_TCATCH) {
			error_tok_code(E_TEMPLATE, &tok,
			    "expected 'catch' after 'try'");
			break;
		}
		catch_is_ref = 0;  /* reset per-catch */
		if (bnext)
			funclabel(f, bnext); /* previous catch's miss lands here */
			next(); /* consume 'catch' */
			expect(TLPAREN, "after 'catch'");
			ctype = NULL;
			if (strcmp(tokenstr(tok.kind), "...") == 0) {
				next(); /* consume '...' */
				expect(TRPAREN, "after catch(...)");
				/* catch-all: no param, matches anything, runs the body,
				 * then terminates the sequence (C++ requires it last).
				 * bnext is cleared so the rethrow-after-loop does not
				 * re-label the previous catch's miss onto the rethrow. */
				stmt(f, s);
				funcjmp(f, bjoin);
				bnext = NULL;
				break;
			} else {
				const char *ts;
				struct type *tt;
				/* Skip leading const/volatile on the catch type:
				 * catch(const T) / catch(const T&)  binds the same
				 * parameter as the unqualified form — C++ ignores the
				 * top-level cv-qualifier when matching the catch type,
				 * and our T&→T* downgrade makes const transparent to
				 * the pointer cast in the binding.  `const`/`volatile`
				 * are C keywords (TCONST/TVOLATILE, below TIDENT), so
				 * check tok.kind directly — cpp_tok_kind() maps these
				 * to CPP_TNONE. */
				while (tok.kind == TCONST || tok.kind == TVOLATILE)
					next();
				ts = tokenstr(tok.kind);
				if (strcmp(ts, "int") == 0)
					ctype = &typeint;
				else if (strcmp(ts, "char") == 0)
					ctype = &typechar;
				else if (strcmp(ts, "long") == 0)
					ctype = &typelong;
				else if (strcmp(ts, "short") == 0)
					ctype = &typeshort;
				else if (strcmp(ts, "double") == 0)
					ctype = &typedouble;
				else {
					tt = scopegettag(s, ts, true);
					if (!tt)
						tt = scopegettag(&filescope, ts, true);
					if (!tt)
						error_tok_code(E_TEMPLATE, &tok,
						    "unknown catch parameter type");
					ctype = tt;
				}
				next(); /* consume the type name */
				/* Catch(T&) is supported as a binding to the carried
				 * runtime object (no copy).  mcc has no real reference
				 * type yet; we model it with a T* local so the catch body
				 * reads via (*e).x.  Type matching keeps using `ctype`
				 * so the type-code/derived match stays unchanged. */
				if (tok.kind == TBAND) {
					next(); /* consume '&' */
					catch_is_ref = 1;
					name[0] = '\0';
					if (tok.kind != TRPAREN) {
						snprintf(name, sizeof name,
						    "%s", tokenstr(tok.kind));
						next();
					}
					expect(TRPAREN,
					    "after catch parameter (&)");
				} else if (tok.kind != TRPAREN) {
					snprintf(name, sizeof name, "%s", tokenstr(tok.kind));
					next();
					expect(TRPAREN, "after catch parameter");
				} else {
					name[0] = '\0';
				}
				if (ctype) {
					/* branch: if (caught_type() == tc(ctype) OR any
					 * registered derived type) hit else miss.  A
					 * catch(Base) matches a throw(Derived) because we
					 * widen the condition over every type already
					 * registered in the typecode table that is derived
					 * from the catch type (cpp_is_derived; base appears
					 * as an anonymous member).  This gives single-inheri
					 * tance base-catch without a full RTTI/typeinfo.
					 *
					 * Scalar-rank widening (integer same-size): for an
					 * integer catch type, also match any registered
					 * integer type of the same size (throw int can be
					 * caught by catch short and vice versa; the cast is
					 * done by exprconvert at the assignment). */
					int i;
					ctx = cpp_exc_helper_call("_meuos_exc_caught_type",
					    &typeint, NULL, NULL);
					cnd = mkbinaryexpr(&tok.loc, TEQL, ctx,
					    mkconstexpr(&typeint, cpp_exc_typecode(ctype)));
					for (i = 0; i < exc_tc_n; i++) {
						if (exc_tc_regs[i].t == ctype)
							continue;
						if (cpp_is_derived(exc_tc_regs[i].t, ctype))
							cnd = mkbinaryexpr(&tok.loc, TLOR, cnd,
							    mkbinaryexpr(&tok.loc, TEQL,
							        ctx,
							        mkconstexpr(&typeint,
							            exc_tc_regs[i].code)));
						}
						/* integer widening is NOT implicit in C++ catch type
					 * matching (the standard says catch is exact-type +
					 * derived-class only).  The assignment of the caught
					 * value to the catch parameter is still handled by
					 * exprconvert below, which does standard integer
					 * conversions on the value.  So throw(int) will NOT
					 * be caught by catch(long) — the type-code mismatch
					 * causes the catch to miss and fall through to the
					 * next handler.  This matches C++ standard semantics
					 * (no implicit promotion/truncation in catch).
					 * Throwing a short and catching an int is rejected
					 * at the type-code level; the value, if the exact
					 * type matched, is widened by exprconvert. */
					bhit = mkblock("exc_catch_hit");
					bmiss = mkblock("exc_catch_miss");
					funcbranch(f, cnd, bhit, bmiss);
					funclabel(f, bhit);
					/* declare the catch param.  By-value: T e, then
					 * initialise from caught_value / caught_obj.  By-ref:
					 * T *e, then bind to the runtime heap object
					 * (no copy).  catch_is_ref is set by the parser
					 * above when '&' was consumed. */
					if (catch_is_ref)
						ed = mkdecl(name, DECLOBJECT,
						    mkpointertype(ctype, QUALNONE),
						    QUALNONE, LINKNONE);
					else
						ed = mkdecl(name, DECLOBJECT, ctype,
						    QUALNONE, LINKNONE);
					ed->u.obj.storage = SDAUTO;
					funcinit(f, ed, NULL, false);
					scopeputdecl(s, ed);
					if (catch_is_ref) {
						/* T *e = (T *)_meuos_exc_caught_obj();
						 * No copy; access via (*e).x.  Only when the
						 * active exception IS an object (scalar-throw
						 * with the same typecode would set caught_obj
						 * to NULL via the scalar path; guard against
						 * deref).  For class types caught_is_obj is
						 * true; for the (rare) scalar-throw with the
						 * same typecode caught_obj stays NULL.
						 *
						 * Base-subobject slicing: when this catch is a
						 * base type (Base&) matching a derived throw
						 * (D), the runtime carries the D object; we
						 * adjust the pointer to the Base sub-object
						 * (mcc-side slice, does not rely on the libc
						 * offset argument).  A local __exc_slice offset
						 * selects the base offset for whatever derived
						 * type actually matched, defaulting to 0 (the
						 * catch type itself, or an unambiguous base at
						 * offset 0). */
						struct expr *ep, *cvs;
						struct expr *isobj;
						struct block *bp1, *bp0, *bpjoin;
						ep = mkexpr(EXPRIDENT,
						    mkpointertype(ctype, QUALNONE),
						    NULL);
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						isobj = cpp_exc_helper_call(
						    "_meuos_exc_caught_is_obj",
						    &typeint, NULL, NULL);
						bp1 = mkblock("exc_ref_obj");
						bp0 = mkblock("exc_ref_no");
						bpjoin = mkblock("exc_ref_join");
						funcbranch(f, isobj, bp1, bp0);
						funclabel(f, bp1);
						/* T *e = slice-view pointer into the carried object
						 * (mcc-side base-subobject slice: selects __exc_slice
						 * per matching derived type, then
						 * (T*)((char*)caught_obj + __exc_slice)). */
						cvs = exc_slice_ptr(f, ctype);
						funcexpr(f, mkassignexpr(ep, cvs));
						funcjmp(f, bpjoin);
						funclabel(f, bp0);
						funcjmp(f, bpjoin);
						funclabel(f, bpjoin);
					} else if (ctype->kind != TYPESTRUCT &&
					    ctype->kind != TYPEUNION) {
						/* scalar catch: copy caught_value into the param */
						struct expr *ep = mkexpr(EXPRIDENT, ctype, NULL);
						struct expr *val;
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						val = cpp_exc_helper_call("_meuos_exc_caught_value",
						    &typeullong, NULL, NULL);
						funcexpr(f, mkassignexpr(ep, exprconvert(val, ctype)));
					} else {
						/* class catch param: rebuild the object from the
						 * runtime's carried heap object (phase 4).  The
						 * catch parameter is copy-initialized from
						 * `*(T *)_meuos_exc_caught_obj()`.  Only when the
						 * active exception IS an object (caught_is_obj) is
						 * a pointer carried; a scalar-caught class (older
						 * type-code-only path) leaves the param
						 * default/uninitialized (no crash on NULL).  The
						 * carried heap object is then released
						 * (dtor+free) via _meuos_exc_caught_free.  For
						 * byte-copyable classes struct assignment copies;
						 * user copy ctors are a later increment (4b). */
						struct expr *ep, *cvs, *deref;
						struct expr *isobj;
						struct block *bp1, *bp0, *bpjoin;
						ep = mkexpr(EXPRIDENT, ctype, NULL);
						ep->lvalue = true;
						ep->u.ident.decl = ed;
						isobj = cpp_exc_helper_call("_meuos_exc_caught_is_obj",
						    &typeint, NULL, NULL);
						bp1 = mkblock("exc_param_obj");
						bp0 = mkblock("exc_param_scalar");
						bpjoin = mkblock("exc_param_join");
						funcbranch(f, isobj, bp1, bp0);
						funclabel(f, bp1);
						/* copy the catch parameter from the slice-view of the
						 * carried object: *(T*)((char*)caught_obj + __exc_slice).
						 * The slice re-aims at the matching base sub-object so a
						 * catch(Base) by value copies Base's bytes, not the head of
						 * the derived object (same slice logic as the by-ref path). */
						cvs = exc_slice_ptr(f, ctype);
						deref = mkunaryexpr(TMUL, cvs);
						funcexpr(f, mkassignexpr(ep, deref));
						funcjmp(f, bpjoin);
						funclabel(f, bp0);
						/* scalar-or-none payload: leave param uninitialised */
						funcjmp(f, bpjoin);
						funclabel(f, bpjoin);
					}
					stmt(f, s); /* the catch body */
					if (ctype && ctype->kind == TYPESTRUCT ||
					    ctype && ctype->kind == TYPEUNION) {
						/* release the runtime-carried heap object after the
						 * catch consumed it (dtor + free; idempotent for a
						 * scalar exception) */
						funcexpr(f, cpp_exc_helper_call(
						    "_meuos_exc_caught_free", &typevoid, NULL, NULL));
					}
					funcjmp(f, bjoin);
				} else {
					/* catch(...) — catch-all: matches every thrown
					 * value, no parameter, runs the body.  C++ requires
					 * it last, so no further catch follows.  Clear bnext
					 * so the rethrow-after-loop doesn't re-label the
					 * previous catch's miss onto the rethrow: the
					 * catch-all already swallowed everything. */
					stmt(f, s);
					funcjmp(f, bjoin);
					bnext = NULL;
					break;
				}
				bnext = bmiss; /* next catch (or rethrow) lands on the miss */
			}
			if (cpp_tok_kind() != CPP_TCATCH)
				break;
		}
		/* no catch matched: rethrow the current exception (stage 3-4
		 * will make this precise; rethrow targets an outer handler or
		 * aborts if none, which matches uncaught semantics). */
		if (bnext)
			funclabel(f, bnext);
		{
			struct expr *ct, *cv, *rt;
			ct = cpp_exc_helper_call("_meuos_exc_caught_type", &typeint,
			    NULL, NULL);
			cv = cpp_exc_helper_call("_meuos_exc_caught_value", &typeullong,
			    NULL, NULL);
			rt = cpp_exc_helper_call("_meuos_exc_throw", &typevoid, ct, cv);
			funcexpr(f, rt);
		}
	}
	funclabel(f, bjoin);
	s = delscope(s);
}


/* C++ `throw` expression (`throw expr;` or bare `throw;` rethrow).
 *
 * `throw expr` lowers to `_meuos_exc_throw(typecode(expr), value)`.  A bare
 * `throw;` (no operand) rethrows the currently-handled exception — in the
 * value-passing ABI that is simply re-raising (caught_type, caught_value),
 * which the runtime routes to the next outer handler or aborts if none.
 */
struct expr *
cpp_parse_throw_expr(struct scope *s)
{
	extern struct expr *assignexpr(struct scope *);
	struct expr *e = NULL;

	next(); /* consume 'throw' */
	if (tok.kind != TSEMICOLON && tok.kind != TEOF)
		e = assignexpr(s);
	if (e)
		return cpp_exc_throw_call(e->type, e);
	/* bare `throw;` — rethrow the current exception */
	{
		struct expr *ct, *cv;
		ct = cpp_exc_helper_call("_meuos_exc_caught_type", &typeint,
		    NULL, NULL);
		cv = cpp_exc_helper_call("_meuos_exc_caught_value", &typeullong,
		    NULL, NULL);
		return cpp_exc_throw_call2(ct, cv);
	}
}
