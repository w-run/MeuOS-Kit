/* cpp_module.c - m++ (C++) module/import/export declarations.
 *
 * Extracted from cpp_parse.c (split into per-domain submodules).
 * C++20 module system syntax parsing only - no semantic module loading.
 * Handles:
 *   - module ModuleName;     (cpp_module_decl)
 *   - module :private;       (cpp_module_decl)
 *   - import ModuleName;     (cpp_import_decl)
 *   - import "header";       (cpp_import_decl, C++23 header import)
 *   - export module ...;     (cpp_export_decl)
 *   - export import ...;     (cpp_export_decl)
 *   - export { ... };        (cpp_export_decl)
 *   - export template ...;   (cpp_export_decl)
 *   - export declaration;    (cpp_export_decl)
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

/* C++20 module declaration: `module ModuleName;` or `module :private;`.
 * Only syntax parsing — no semantic module loading. */
void
cpp_module_decl(struct scope *s)
{
	next(); /* consume 'module' */

	if (tok.kind == TCOLON) {
		/* `module :private;` — private module fragment */
		next(); /* consume ':' */
		if (cpp_tok_kind() != CPP_TPRIVATE)
			error_code(E_SYNTAX, &tok.loc, "expected 'private' after 'module :'");
		next(); /* consume 'private' */
		expect(TSEMICOLON, "after module :private");
		return;
	}

	/* Parse module name: identifier (. identifier)* */
	if (tok.kind >= TIDENT) {
		for (;;) {
			next(); /* consume identifier */
			if (tok.kind == TPERIOD) {
				next(); /* consume '.' */
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected identifier after '.' in module name");
			} else {
				break;
			}
		}
	}
	expect(TSEMICOLON, "after module declaration");
}

/* C++20 import declaration: `import ModuleName;` or `import "header";`.
 * Only syntax parsing — no semantic module loading. */
void
cpp_import_decl(struct scope *s)
{
	next(); /* consume 'import' */

	/* C++23 header import: `import "header";` */
	if (tok.kind == TSTRINGLIT) {
		next(); /* consume string literal */
		expect(TSEMICOLON, "after header import");
		return;
	}

	/* Parse module name: identifier (. identifier)* */
	if (tok.kind >= TIDENT) {
		for (;;) {
			next(); /* consume identifier */
			if (tok.kind == TPERIOD) {
				next(); /* consume '.' */
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected identifier after '.' in module name");
			} else {
				break;
			}
		}
	}
	expect(TSEMICOLON, "after import declaration");
}

/* C++20 export declaration: `export module ...`, `export import ...`,
 * `export { ... }`, `export declaration`, or `export template ...`.
 * Only syntax parsing — no semantic export tracking. */
void
cpp_export_decl(struct scope *s)
{
	next(); /* consume 'export' */

	enum cpp_tokenkind k = cpp_tok_kind();
	if (k == CPP_TMODULE) {
		/* `export module ModuleName;` — module interface declaration */
		cpp_module_decl(s);
	} else if (k == CPP_TIMPORT) {
		/* `export import ModuleName;` — re-export an imported module */
		cpp_import_decl(s);
	} else if (tok.kind == TLBRACE) {
		/* `export { ... }` — export block */
		next(); /* consume '{' */
		while (tok.kind != TRBRACE && tok.kind != TEOF) {
			enum cpp_tokenkind k2 = cpp_tok_kind();
			if (k2 == CPP_TEXPORT)
				cpp_export_decl(s);
			else if (k2 == CPP_TIMPORT)
				cpp_import_decl(s);
			else if (k2 == CPP_TMODULE)
				cpp_module_decl(s);
			else if (k2 == CPP_TUSING)
				cpp_using_decl(s);
			else if (k2 == CPP_TTEMPLATE)
				cpp_template_decl(s, NULL);
			else if (k2 == CPP_TCLASS || k2 == CPP_TSTRUCT || k2 == CPP_TUNION)
				cpp_class_decl(s);
			else if (cpp_is_namespace_decl())
				cpp_namespace_decl(s);
			else
				decl(s, NULL);
		}
		if (tok.kind == TRBRACE)
			next(); /* consume '}' */
	} else if (k == CPP_TTEMPLATE) {
		/* `export template <...> ...` */
		cpp_template_decl(s, NULL);
	} else {
		/* `export declaration` — parse the declaration normally */
		decl(s, NULL);
	}
}