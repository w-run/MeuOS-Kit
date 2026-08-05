/* cpp_gcctor.c — m++ (C++) global class-typed object construction.
 *
 * Collects file-scope objects with user constructors and emits
 * __mxx_global_var_init (wired to .init_array) to construct them before
 * main; cpp_emit_global_ctors is called at the end of the translation
 * unit, cpp_record_global_ctor from the C front-end (decl.c).
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

/* Global class-typed objects with user constructors; their construction
 * calls are collected and emitted into __mxx_global_var_init (wired to
 * the .init_array section so the runtime runs them before main). */
struct cpp_global_ctor {
	struct decl *d;
	struct expr *args;   /* ctor call arguments (may be NULL) */
	struct cpp_global_ctor *next;
};
static struct cpp_global_ctor *g_cpp_global_ctors;
static struct cpp_global_ctor **g_cpp_global_ctors_end =
    &g_cpp_global_ctors;

void
cpp_record_global_ctor(struct decl *d, struct expr *args)
{
	struct cpp_global_ctor *g;
	const char *tag;

	if (!d || !d->type || (d->type->kind != TYPESTRUCT && d->type->kind != TYPEUNION))
		return;
	tag = d->type->u.structunion.tag;
	if (!tag || !cpp_has_ctor(d->type, tag))
		return;
	g = xmalloc(sizeof *g);
	g->d = d;
	g->args = args;
	g->next = NULL;
	*g_cpp_global_ctors_end = g;
	g_cpp_global_ctors_end = &g->next;
}

/* Emit `void __mxx_global_var_init(void)` that runs every recorded
 * global constructor, then place its address in .init_array. */
void
cpp_emit_global_ctors(void)
{
	extern struct scope filescope;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern void funcret(struct func *, struct value *);
	extern struct scope *delscope(struct scope *);
	extern void tokpush(struct token *, size_t);

	struct cpp_global_ctor *g;
	struct decl *d;
	struct scope *fs;
	struct func *f;
	struct type *vt;

	if (!g_cpp_global_ctors)
		return;

	vt = mktype(TYPEFUNC, 0);
	vt->base = &typevoid;
	vt->u.func.params = NULL;
	vt->u.func.nparam = 0;
	vt->u.func.isvararg = false;   /* mktype leaves u.func uninitialized */

	d = mkdecl("__mxx_global_var_init", DECLFUNC, vt, QUALNONE, LINKEXTERN);
	d->value = mkglobal(d);
	fs = mkscope(&filescope);
	f = mkfunc(d, d->name, vt, fs);
	for (g = g_cpp_global_ctors; g; g = g->next)
		cpp_emit_ctor_call(f, g->d, g->args);
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);

	/* register in .init_array so the runtime calls it before main */
	printf(".section .init_array,\"aw\"\n");
	printf(".balign 8\n");
	printf(".quad __mxx_global_var_init\n");

	/* Reverse-order destruction: `void __mxx_global_var_fini(void)`
	 * calls each global's destructor in reverse construction order and
	 * is registered in .fini_array so the runtime runs it after main. */
	vt = mktype(TYPEFUNC, 0);
	vt->base = &typevoid;
	vt->u.func.params = NULL;
	vt->u.func.nparam = 0;
	vt->u.func.isvararg = false;

	d = mkdecl("__mxx_global_var_fini", DECLFUNC, vt, QUALNONE, LINKEXTERN);
	d->value = mkglobal(d);
	fs = mkscope(&filescope);
	f = mkfunc(d, d->name, vt, fs);
	{
		/* reverse traversal: two pointers walk the list */
		struct cpp_global_ctor *tail = NULL, *cur = g_cpp_global_ctors;
		while (cur) {
			struct cpp_global_ctor *next = cur->next;
			cur->next = tail;
			tail = cur;
			cur = next;
		}
		for (g = tail; g; g = g->next)
			cpp_emit_global_dtor(f, g->d);
	}
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);

	printf(".section .fini_array,\"aw\"\n");
	printf(".balign 8\n");
	printf(".quad __mxx_global_var_fini\n");
}
