/* cpp_using.c - m++ (C++) using-declarations.
 *
 * Extracted from cpp_parse.c (split into per-domain submodules).
 * Handles:
 *   - using namespace NAME;          (using-directive, makes ns members visible)
 *   - using NAME::member;             (using-declaration, brings member into scope)
 *   - using Name = Type;              (C++11 alias declaration)
 *   - using enum E;                   (C++20 using-enum-declaration)
 *
 * Non-static so the shared C declaration parser can dispatch
 * block-scope `using` (and thus for/if init-statement alias
 * declarations, P2360) to the C++ frontend.
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

/* `using namespace NAME;`, `using NAME::member;`, or a C++11 alias
 * declaration `using Name = Type;`.  Non-static so the shared C
 * declaration parser can dispatch block-scope `using` (and thus for/if
 * init-statement alias declarations, P2360) to the C++ frontend. */
void
cpp_using_decl(struct scope *s)
{
	next(); /* consume 'using' */
	/* C++20 `using enum E;` — bring all enumerators of E into
	 * the current scope.  `enum` is a C keyword (TENUM), so the
	 * C++ lexer token is used directly. */
	if (tok.kind == TENUM) {
		const char *etag;
		struct type *et;
		size_t i;
		next(); /* consume 'enum' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected enum name after 'using enum'");
		etag = tokenstr(tok.kind);
		et = scopegettag(s, etag, 1);
		if (!et || et->kind != TYPEENUM)
			error_code(E_CTYPE, &tok.loc, "'%s' is not an enum type", etag);
		next(); /* consume enum name */
		expect(TSEMICOLON, "after using enum declaration");
		if (et->incomplete)
			error_code(E_INCOMPLETE, &tok.loc, "cannot use incomplete enum type '%s'", etag);
		/* Bring all enumerators (DECLCONST decls) of this enum type
		 * into the current scope.  Walk the scope chain looking for
		 * DECLCONST decls whose type matches the enum.  Since the map
		 * is a hash table, we iterate over the current scope's decl
		 * map directly. */
		for (i = 0; i < s->decls.cap; i++) {
			if (s->decls.keys[i].str) {
				struct decl *d = s->decls.vals[i].p;
				if (d && d->kind == DECLCONST && d->type == et) {
					/* Re-insert into the same scope (already
					 * visible, but ensures lookup works). */
					scopeputdecl(s, d);
				}
			}
		}
		return;
	}
	if (cpp_tok_kind() == CPP_TNAMESPACE) {
		struct decl *nsd;
		next(); /* consume 'namespace' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected namespace name after 'using namespace'");
		nsd = scopegetdecl(s, tokenstr(tok.kind), 1);
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error_code(E_CTYPE, &tok.loc, "'%s' is not a namespace", tokenstr(tok.kind));
		cpp_add_visible_ns(nsd->u.ns);
		next();
		expect(TSEMICOLON, "after using directive");
		return;
	}
	/* using NAME::member; or using Name = Type; */
	{
		struct decl *nsd;
		const char *nm;
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected namespace name in using declaration");
		nm = tokenstr(tok.kind);
		nsd = scopegetdecl(s, nm, 1);
		next();
		if (tok.kind == TASSIGN) {
			/* C++11 alias declaration: `using Name = Type;` */
			struct type *at;
			next(); /* consume '=' */
			at = typename(s, NULL, NULL);
			if (!at)
				error_code(E_SYNTAX, &tok.loc, "expected type name in alias declaration");
			expect(TSEMICOLON, "after alias declaration");
			scopeputdecl(s, mkdecl((char *)nm, DECLTYPE, at, QUALNONE, LINKNONE));
			return;
		}
		expect(TCOLONCOLON, "after namespace name in using declaration");
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected member name after '::'");
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error_code(E_CTYPE, &tok.loc, "'%s' is not a namespace", nsd ? nsd->name : "?");
		{
			struct decl *md = scopegetdecl(nsd->u.ns, tokenstr(tok.kind), 1);
			if (!md)
				error_code(E_CTYPE, &tok.loc, "no member named '%s' in namespace '%s'",
				      tokenstr(tok.kind), nsd->name);
			scopeputdecl(s, md);
		}
		next();
		expect(TSEMICOLON, "after using declaration");
	}
}