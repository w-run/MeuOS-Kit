/* cpp_scan.c — m++ (C++) lexer.
 *
 * Stage C.1.2: a C++ keyword table and a scan routine that classifies an
 * identifier token as a C++ keyword or a plain identifier.  The C lexer
 * (src/lex/scan.c) produces the raw tokens; the C++ layer re-interprets
 * identifiers against the C++ keyword table, and handles C++-only
 * punctuators (->*, .*, ::, <<=, >>=, <=>) that the C lexer does not
 * recognize.
 *
 * The design keeps the C lexer as the base (C++ is a C superset for
 * tokenization) and layers C++ classification on top — matching the
 * "parse from C, layer C++ on top" strategy in the m++ architecture.
 */
#include <stdio.h>
#include <string.h>

#include "cpp/cpp_tokens.h"

/* C++ keyword table: name -> cpp_tokenkind.  Sorted by name for binary
 * search; entries are kept in ASCII order. */
struct cpp_kw {
	const char *name;
	enum cpp_tokenkind kind;
};

static const struct cpp_kw cpp_kws[] = {
	{ "alignas", CPP_TALIGNAS },
	{ "alignof", CPP_TALIGNOF },
	{ "auto", CPP_TAUTO },
	{ "bool", CPP_TBOOL },
	{ "break", CPP_TBREAK },
	{ "case", CPP_TCASE },
	{ "catch", CPP_TCATCH },
	{ "char", CPP_TCHAR },
	{ "class", CPP_TCLASS },
	{ "co_await", CPP_TCO_AWAIT },
	{ "co_return", CPP_TCO_RETURN },
	{ "co_yield", CPP_TCO_YIELD },
	{ "concept", CPP_TCONCEPT },
	{ "const", CPP_TCONST },
	{ "const_cast", CPP_TCONST_CAST },
	{ "constexpr", CPP_TCONSTEXPR },
	{ "continue", CPP_TCONTINUE },
	{ "decltype", CPP_TDECLTYPE },
	{ "default", CPP_TDEFAULT },
	{ "delete", CPP_TDELETE },
	{ "do", CPP_TDO },
	{ "double", CPP_TDOUBLE },
	{ "dynamic_cast", CPP_TDYNAMIC_CAST },
	{ "else", CPP_TELSE },
	{ "enum", CPP_TENUM },
	{ "explicit", CPP_TEXPLICIT },
	{ "export", CPP_TEXPORT },
	{ "extern", CPP_TEXTERN },
	{ "false", CPP_TFALSE },
	{ "final", CPP_TFINAL },
	{ "float", CPP_TFLOAT },
	{ "for", CPP_TFOR },
	{ "friend", CPP_TFRIEND },
	{ "goto", CPP_TGOTO },
	{ "if", CPP_TIF },
	{ "import", CPP_TIMPORT },
	{ "inline", CPP_TINLINE },
	{ "int", CPP_TINT },
	{ "long", CPP_TLONG },
	{ "module", CPP_TMODULE },
	{ "mutable", CPP_TMUTABLE },
	{ "namespace", CPP_TNAMESPACE },
	{ "new", CPP_TNEW },
	{ "noexcept", CPP_TNOEXCEPT },
	{ "nullptr", CPP_TNULLPTR },
	{ "nullptr_t", CPP_TNULLPTR_T },
	{ "operator", CPP_TOPERATOR },
	{ "override", CPP_TOVERRIDE },
	{ "private", CPP_TPRIVATE },
	{ "protected", CPP_TPROTECTED },
	{ "public", CPP_TPUBLIC },
	{ "register", CPP_TREGISTER },
	{ "reinterpret_cast", CPP_TREINTERPRET_CAST },
	{ "requires", CPP_TREQUIRES },
	{ "return", CPP_TRETURN },
	{ "short", CPP_TSHORT },
	{ "signed", CPP_TSIGNED },
	{ "sizeof", CPP_TAUTO }, /* sizeof is C; mapped to auto for now (unused) */
	{ "static", CPP_TSTATIC },
	{ "static_assert", CPP_TSTATIC_ASSERT },
	{ "static_cast", CPP_TSTATIC_CAST },
	{ "struct", CPP_TSTRUCT },
	{ "switch", CPP_TSWITCH },
	{ "template", CPP_TTEMPLATE },
	{ "this", CPP_TTHIS },
	{ "thread_local", CPP_TTHREAD_LOCAL },
	{ "throw", CPP_TTHROW },
	{ "true", CPP_TTRUE },
	{ "try", CPP_TTRY },
	{ "typedef", CPP_TTYPEDEF },
	{ "typeid", CPP_TAUTO },
	{ "typename", CPP_TTYPENAME },
	{ "union", CPP_TUNION },
	{ "unsigned", CPP_TUNSIGNED },
	{ "using", CPP_TUSING },
	{ "virtual", CPP_TVIRTUAL },
	{ "void", CPP_TVOID },
	{ "volatile", CPP_TVOLATILE },
	{ "while", CPP_TWHILE },
};

#define CPP_NKWS ((int)(sizeof cpp_kws / sizeof cpp_kws[0]))

/* Classify an identifier.  Returns the C++ token kind (>= CPP_TNONE) if
 * the identifier is a C++ keyword, or CPP_TNONE for a plain identifier. */
enum cpp_tokenkind
cpp_classify_ident(const char *name, size_t len)
{
	int lo = 0, hi = CPP_NKWS - 1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int cmp = strncmp(name, cpp_kws[mid].name, len);
		if (cmp == 0) {
			/* exact length match required (keyword name may be longer) */
			if (cpp_kws[mid].name[len] == '\0')
				return cpp_kws[mid].kind;
			cmp = len - (int)strlen(cpp_kws[mid].name);
		}
		if (cmp < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return CPP_TNONE;
}

/* Return the keyword table size (for tests). */
int
cpp_nkeywords(void)
{
	return CPP_NKWS;
}
