/* cpp_parse.c — m++ (C++) parser entry.
 *
 * Stage C.1.3: the C++ parser entry point.  C++ is parsed as a C
 * superset — the C parser (src/parse/) handles C-compatible constructs,
 * and C++-only constructs (class/namespace/template/...) are layered on
 * top here.  The translation-unit loop calls this instead of the C
 * `decl()` loop when the input language is C++ (selected by the m++
 * driver).
 *
 * Currently this is a minimal skeleton: it delegates to the C parser for
 * the full translation unit.  C++ grammar handling (class member
 * declarations, access specifiers, namespace blocks, templates) is added
 * incrementally in later stages.
 */
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"

/* Classify the current token as a C++ keyword, if any.  Wired to the C++
 * lexer's keyword table; the C lexer tokenizes identifiers, and this
 * re-interprets them as C++ keywords for the parser. */
enum cpp_tokenkind
cpp_tok_kind(void)
{
	if (tok.kind == TIDENT)
		return cpp_classify_ident(tok.lit, tok.lit ? strlen(tok.lit) : 0);
	return CPP_TNONE;
}

/* Parse a C++ translation unit: top-level declaration loop.
 * C++ grammar is layered over the C parser; currently C-compatible
 * declarations are parsed by the shared C parser. */
void
cpp_parse_translation_unit(void)
{
	extern struct scope filescope;
	extern void emittentativedefns(void);

	while (tok.kind != TEOF) {
		if (!decl(&filescope, NULL)) {
			if (tok.kind == TSEMICOLON)
				error(&tok.loc, "unexpected ';' at top-level");
			error(&tok.loc, "expected declaration or function definition");
		}
	}
	emittentativedefns();
}
