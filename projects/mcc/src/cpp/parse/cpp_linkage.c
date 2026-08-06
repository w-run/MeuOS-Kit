/* cpp_linkage.c - m++ (C++) linkage specifications.
 *
 * Extracted from cpp_parse.c (split into per-domain submodules).
 * Handles the C++ `extern "C"` linkage specification, both forms:
 *
 *   extern "C" { ... }       -- block form, all inner decls have C linkage
 *   extern "C" int f();      -- single-declaration form
 *
 * Intercepted before the C parser's decl() sees `extern` as a
 * storage class.  The peek-ahead saves the extern token, consumes
 * the next token, and checks for the `"C"` string literal; if not
 * extern "C", the same pushback pattern as consume() in pp.c is
 * used (copy the peeked token to a local, restore the original,
 * then ctxpush the copy) so the C parser sees `extern int ...`.
 *
 * The `g_cpp_extern_c` flag is set for the duration of the inner
 * declarations so that getlinkage() in decl.c assigns LINKC
 * instead of LINKEXTERN.  Nested `extern "C"` is supported via
 * `strdup("nested")` so the outer flag can be restored correctly.
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

/* C++ `extern "C"` linkage context: true when the current declaration
 * is inside an `extern "C"` block or is preceded by `extern "C"`.  Used
 * by getlinkage() in decl.c (with `extern bool g_cpp_extern_c;` at the
 * use site) to assign LINKC instead of LINKEXTERN. */
bool g_cpp_extern_c;

/* Try to consume a C++ `extern "C"` linkage specification.  Returns
 * true if consumed (tok is positioned past the linkage spec, the
 * caller should re-enter its outer loop); returns false if the
 * token stream did not actually start with `extern "C"` (the
 * `extern` token has been restored in that case, so the caller
 * falls through to the normal C parser). */
bool
cpp_linkage_spec(void)
{
	extern struct scope filescope;
	extern int g_lang;
	struct token save, peek;

	if (tok.kind != TEXTERN || g_lang != 1)
		return false;
	save = tok;
	next();
	peek = tok;
	/* The C lexer stores string literal content INCLUDING the
	 * surrounding quotes in tok.lit, so we compare against "\"C\""
	 * (the literal text `"C"` with quote characters). */
	if (!(peek.kind == TSTRINGLIT && peek.lit &&
	    peek.lit[0] == '"' && peek.lit[1] == 'C' && peek.lit[2] == '"' && peek.lit[3] == '\0')) {
		/* not `extern "C"` — restore the extern token and push the
		 * peeked token back so the normal C parser sees `extern int ...`. */
		tok = save;
		tokpush(&peek, 1);
		return false;
	}
	/* extern "C": consume the "C" token (tok currently points to
	 * it because next() advanced past extern). */
	next(); /* consume "C" string literal */
	char *saved = g_cpp_extern_c ? strdup("nested") : NULL;
	if (tok.kind == TLBRACE) {
		/* `extern "C" { ... }` — parse all declarations inside
		 * the block with C linkage, then restore. */
		next(); /* consume '{' */
		g_cpp_extern_c = true;
		while (tok.kind != TRBRACE && tok.kind != TEOF) {
			enum cpp_tokenkind k2 = cpp_tok_kind();
			if (k2 == CPP_TCLASS || k2 == CPP_TSTRUCT ||
			    k2 == CPP_TUNION) {
				if (k2 == CPP_TCLASS || cpp_struct_needs_class_decl())
					cpp_class_decl(&filescope);
				else
					decl(&filescope, NULL);
			} else if (cpp_is_namespace_decl()) {
				cpp_namespace_decl(&filescope);
			} else if (k2 == CPP_TUSING) {
				cpp_using_decl(&filescope);
			} else if (k2 == CPP_TTEMPLATE) {
				cpp_template_decl(&filescope, NULL);
			} else {
				decl(&filescope, NULL);
			}
		}
		if (tok.kind == TRBRACE)
			next(); /* consume '}' */
		g_cpp_extern_c = saved ? true : false;
		if (saved)
			free(saved);
	} else {
		/* `extern "C" int f();` — single declaration with C linkage.
		 * Set the flag, parse the declaration, then restore. */
		g_cpp_extern_c = true;
		decl(&filescope, NULL);
		g_cpp_extern_c = saved ? true : false;
		if (saved)
			free(saved);
	}
	return true;
}