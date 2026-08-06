/* cpp_namespace.c - m++ (C++) namespace declarations.
 *
 * Stage C.1.3: C++ namespace declarations (`namespace NAME { ... }`),
 * inline namespace (C++11), nested namespace lookup, and the qualified
 * class name state for out-of-line method definitions.
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

/* Pending qualified-class name from a `Class::method` declarator; consumed
 * by decl()'s DECLFUNC path to route out-of-line method definitions. */
static const char *g_cpp_qual_class;
/* Namespace the qualified class lives in (`ns::Class::method`), or NULL
 * for a plain file-scope `Class::method`. */
static struct scope *g_cpp_qual_ns;

void
cpp_set_qual_class(const char *tag)
{
	g_cpp_qual_class = tag;
}

const char *
cpp_take_qual_class(void)
{
	const char *tag = g_cpp_qual_class;
	g_cpp_qual_class = NULL;
	return tag;
}

void
cpp_set_qual_ns(struct scope *ns)
{
	g_cpp_qual_ns = ns;
}

struct scope *
cpp_take_qual_ns(void)
{
	struct scope *ns = g_cpp_qual_ns;
	g_cpp_qual_ns = NULL;
	return ns;
}

/* Qualified assembly prefix for a declaration inside namespace(s):
 * `Outer_Inner` for a name declared in `namespace Outer { namespace Inner
 * { ... } }`, `Geo` for a single-level `namespace Geo`.  Returns NULL if
 * `s` is the file scope (no enclosing namespace).  The caller uses this
 * to give namespace-scope objects/functions a distinct symbol name so a
 * namespace variable does not collide with a same-named global. */
const char *
cpp_ns_asm_prefix(struct scope *s, char *buf, size_t bufsz)
{
	const char *names[16];
	int n = 0, i;

	(void)bufsz;
	for (; s && s->name; s = s->parent) {
		if (n >= (int)countof(names))
			break;
		names[n++] = s->name;
	}
	if (!n)
		return NULL;
	buf[0] = '\0';
	/* names[] is innermost-first; walk it backwards to build
	 * Outer_Inner (outermost first). */
	for (i = n - 1; i >= 0; i--) {
		if (i != n - 1)
			strcat(buf, "_");
		strcat(buf, names[i]);
	}
	return buf;
}

/* Namespaces made visible by `using namespace NAME;` directives and by
 * `inline namespace` blocks.  Lookups that fail in the current scope
 * consult these before giving up. */
static struct scope *g_cpp_visible_ns[16];
static int g_cpp_nvisible_ns;

/* Is the current token the start of a namespace declaration, possibly
 * prefixed by the C++11 `inline` keyword (`inline namespace NAME {`)?
 * Peeks one token ahead for the `inline` case and restores the stream. */
bool
cpp_is_namespace_decl(void)
{
	enum cpp_tokenkind k = cpp_tok_kind();

	if (k == CPP_TNAMESPACE)
		return true;
	if (k == CPP_TINLINE) {
		struct token save = tok;
		struct token peek;
		struct token *tp;
		next();
		peek = tok;
		tok = save;
		/* tokpush stores the token pointer, so the peeked token must
		 * outlive this frame (heap copy, like struct_decl's '&' restore) */
		tp = xmalloc(sizeof *tp);
		*tp = peek;
		tokpush(tp, 1);
		return cpp_classify_token(peek) == CPP_TNAMESPACE;
	}
	return false;
}

void
cpp_namespace_decl(struct scope *s)
{
	struct scope *ns;
	struct decl *nd;
	const char *name;
	bool is_inline = false;

	if (cpp_tok_kind() == CPP_TINLINE) {
		is_inline = true;
		next(); /* consume 'inline' */
	}
	next(); /* consume 'namespace' */
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc, "expected namespace name");
	name = tokenstr(tok.kind);
	next();
	expect(TLBRACE, "after namespace name");

	ns = mkscope(s);
	ns->name = name;
	nd = mkdecl((char *)name, DECLNAMESPACE, NULL, QUALNONE, LINKNONE);
	nd->u.ns = ns;
	scopeputdecl(s, nd);

	/* inline namespace (C++11): its members are visible in the enclosing
	 * scope, like a `using namespace` directive — but only when the
	 * enclosing scope is itself visible to name lookup (the file scope
	 * or a namespace already brought in by a using-directive), so a
	 * plain `namespace A { inline namespace B { ... } }` does not leak
	 * B's members into the file scope. */
	if (is_inline) {
		extern struct scope filescope;
		bool parent_visible = s == &filescope;
		int i;
		for (i = 0; !parent_visible && i < g_cpp_nvisible_ns; i++)
			if (g_cpp_visible_ns[i] == s)
				parent_visible = true;
		if (parent_visible)
			cpp_add_visible_ns(ns);
	}

	while (tok.kind != TRBRACE && tok.kind != TEOF) {
		enum cpp_tokenkind k = cpp_tok_kind();
		if (cpp_is_namespace_decl()) {
			cpp_namespace_decl(ns);
			continue;
		}
		if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION) {
			if (k == CPP_TCLASS || cpp_struct_needs_class_decl())
				cpp_class_decl(ns);
			else
				decl(ns, NULL);
			continue;
		}
		if (tok.kind == TSEMICOLON) {
			next();
			continue;
		}
		if (!decl(ns, NULL))
			error_code(E_SYNTAX, &tok.loc, "expected declaration in namespace body");
	}
	next(); /* consume '}' */
	/* deliberately keep ns alive for later NAME::name lookups */
}

void
cpp_add_visible_ns(struct scope *ns)
{
	if (g_cpp_nvisible_ns >= (int)countof(g_cpp_visible_ns))
		return;
	g_cpp_visible_ns[g_cpp_nvisible_ns++] = ns;
}

/* Resolve `name` in the visible (`using namespace`) namespaces. */
struct decl *
cpp_lookup_visible(struct scope *s, const char *name)
{
	int i;

	(void)s;
	for (i = 0; i < g_cpp_nvisible_ns; i++) {
		struct decl *d = scopegetdecl(g_cpp_visible_ns[i], name, 1);
		if (d)
			return d;
	}
	return NULL;
}